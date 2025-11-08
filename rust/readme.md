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

