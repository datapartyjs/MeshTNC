# MeshTNC

This is a host tool for interacting with a MeshTNC device.

Currently, it's able to connect to the device using serial
and set the radio settings.  It then remains in rxlog mode
and parses the incoming data.

MeshCore packets are supported via the MeshCore crate that
was recently uploaded.  Some packet types are still missing
support.

As the packets come in over the interface, they're parsed
and if it's possible to find a shared secret that decrypts
them then the payload is also parsed.  The keys must be in
a `toml` file with the following format:

```toml
[[identities]]
name = "Sample 4 TD"
private_key = "f8285b33f3949770f4668be015c325d3ee6dd99c86e44d15ec2a48e157d355470161800938619f83944af343ae996ce433536b324f38e2ae242e8e1cf40649d5"

# ...

[[identities]]
name = "Sample 5 TD"
private_key = "60B176D579DD1F874C38B1AA6476F30BB77E7CCDD325BC718D9C926628B9037445AE4968856CF19453E8D72C19160EB34FDF7E7EC9EFE384A1ACA5573D6F28E5"

[[contacts]]
name = "Sample 1 CT"
public_key = "34569df1f9661916901669666fb8025eccb9ddb0499cddad4c164fec219c8b8f"

# ...

[[contacts]]
name = "Sample 2 CT"
public_key = "12349bdc1f76a0c12149bb15f791dbe42fde02c209b04a85c6f512990c8cedec"

[[groups]]
name = "#bot"
secret = "eb50a1bcb3e4e5d7bf69a57c9dada211"

# ...

[[groups]]
name = "#capitolhill"
secret = "7f281916c8ec32e13c5ef687d182160a"
```

## Installation

The easiest way to install the tool is to install rust
using [rust.up](https://rustup.rs/), then use cargo to
install meshtnc:

```sh
cargo install meshtnc
```

This will install the binary in your local cargo-managed
path.  If running `meshtnc` from the command line yields
a command not found error, ensure that your shell profile
is setup to search in cargo's bin directory.

### Installation for Wireshark

To use the tool as a wireshark extcap interface, you'll
need to make a symlink to the binary in a directory that
wireshark knows to look.  The most reliable way to learn
what this directory is is to open wireshark, then go to
Help->About Wireshark->Folders.  In that list, there will
be a folder labeled "Personal Extcap path" and will be
something like `/home/<your home>/.local/lib/wireshark/extcap`.

Once you know what that folder is, make the directory using
a command similar to:

```bash
mkdir -p ~/.local/lib/wireshark/extcap
```

Then navigate to that folder and symlink meshtnc into it.
This will allow wireshark to find the program and query it
for its available configuration options, and start the interface.

```bash
cd ~/.local/lib/wireshark
ln -s $(which meshtnc) ./
```

Once that's complete, start wireshark, and you should see a new
interface name `MeshCore Monitor: mc0`.  If you click the gear
next to it, you'll be able to check and set some settings
such as the serial port, the radio frequency, bandwidth, etc.

The defaults are set so it should work most of the time with the
US recommended settings and `/dev/ttyUSB0`.  When you cliek Start
the interface will start, and new meshcore packets should come
into the interface.

### Usage

This program can be used in two ways.  One is via the command-line
where it will display packet dissections in real time in a quasi
table.  If you provided a identities file, then those keys will
be used to attempt to decrypt the messages.

The other way it can be used is as a wireshark extcap interface.
This useage is describe somewhat above, but the general idea is that
wireshark treats it as a newtwork interface for packet capture.

It will display those packets in real time on its display, and parse
them up until the encrypted portions.  It is my belief that the
limitations of the lua package ecosystem prevents packet decryption
as a lua plugin to wireshark.  Therefore, you'll only be able to
investigate the cleartext portions of the packets.

