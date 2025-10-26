## MeshTNC

[![experimental](http://badges.github.io/stability-badges/dist/experimental.svg)](http://github.com/badges/stability-badges)[![license](https://img.shields.io/github/license/datapartyjs/MeshTNC)](https://github.com/datapartyjs/MeshTNC/blob/master/license.txt)

MeshTNC is a tool for piping LoRa data to and from consumer grade radios.

 * Code - [github.com/datapartyjs/MeshTNC](https://github.com/datapartyjs/MeshTNC)
 * Releases - [github.com/datapartyjs/MeshTNC/releases](https://github.com/datapartyjs/MeshTNC/releases)
 * Support - [ko-fi/dataparty](https://ko-fi.com/dataparty)

## Features

 - Simple serial cli built into the firmware
 - Transmit raw bytes (hex) over LoRA using serial CLI
 - LoRA packet logging to serial (hex)
 - KISS-TNC mode

## Compiling
- Install [PlatformIO](https://docs.platformio.org) in [Visual Studio Code](https://code.visualstudio.com).
- Clone and open the MeshTNC repository in Visual Studio Code.
- See the example applications you can modify and run:
  - [Simple Repeater](./examples/simple_repeater)

## Flashing

Download precompiled firmware releases - [github.com/datapartyjs/MeshTNC/releases](https://github.com/datapartyjs/MeshTNC/releases)

We haven't built a flashing tool yet. You can flash builds using the meshcore flasher, the OEM provided flashing tools or using the developer instructions to flash using VS Code.

- Flash using your platform's OEM flashing tool
- Use the meshcore flasher - http://flasher.meshcore.co.uk/


## Hardware Compatibility

MeshTNC is designed for devices supported by MeshCore so check their support list in the [MeshCore Flasher](https://flasher.meshcore.co.uk). We support most of the same hardware see [variants](https://github.com/datapartyjs/MeshTNC/tree/main/variants).

## Serial CLI

The MeshTNC firmware initially starts up in a serial mode which is human readable. You can connect to the serial console using a serial terminal application like below.

`minicom -D /dev/ttyACM0`

Once connected the MeshTNC device has a simple CLI. The CLI is largely similar to meshcore with a few notable additions.

### CLI additions

 * `txraw <hex...>` - Transmist a packet
 * `get syncword <word>` - Read the syncword setting
 * `set kiss port <port>` - Set the KISS device port
 * `set radio <freq>,<bw>,<sf>,<coding-rate>,<syncword>` - Configure the radio
 * `serial mode kiss` - Switch to KISS mode
 * `rxlog on`
   * Output format: ` [timestamp],[type=RXLOG],[rssi],[snr],[hex...]\n` 

 <details>
      <summary> Existing Commands</summary>

 * `reboot`
 * `clock sync`
 * `start ota`
 * `clock`
 * `time`
 * `tempradio`
 * `clear stats`
 * `get af`
 * `get agc.reset.interval`
 * `get name`
 * `get lat`
 * `get lon`
 * `get radio`
 * `get rxdelay`
 * `get txdelay`
 * `get tx`
 * `get freq`
 * `set af`
 * `set int.thresh`
 * `set agc.reset.interval`
 * `set name`
 * `set radio`
 * `set lat`
 * `set lon`
 * `set rxdelay`
 * `set txdelay`
 * `set tx`
 * `set freq`
 * `erase`
 * `ver`
 * `log start`
 * `log stop`
 * `rxlog on`
 * `rxlog off`
</details>

### KISS Mode

 * Open a serial console and connect to the MeshTNC device
 * `serial mode kiss`

## APRS over LoRa

<img src="https://github.com/user-attachments/assets/ca4e8caf-5eff-44d3-8ff0-c9d57bfc6ca3" width="40%"></img> <img src="https://github.com/user-attachments/assets/aa4506dd-34b6-4277-af8e-3470ef8f8dfa" width="40%"></img> 

You can use your favorite APRS tools with MeshTNC. Simply select a frequency, place the radio into kiss mode and connect to your APRS tools as a KISS TNC device.

 * `minicom -D /dev/ttyACM0`
   * `set radio 918.25,500.0,7,5,0x16`
   * `serial mode kiss`

### APRS Software

MeshTNC should work with lots of APRS clients, we've tested on the following:

 * [xastir](https://xastir.org/index.php/Main_Page)
 * [APRSisce32](http://aprsisce.wikidot.com/)



## Ethernet over LoRa

<img src="https://github.com/user-attachments/assets/d8347c1c-d76d-469b-89d2-cd2c18859607" width="40%"></img>


Run the following on two or more computers, each with a MeshTNC device attached, to create an ethernet over LoRa network.

 * Install [`tncattach`](https://github.com/markqvist/tncattach)
   * `git clone https://github.com/markqvist/tncattach.git`
   * `cd tncattach`
   * `make`
   * `sudo make install`
 * `minicom -D /dev/ttyACM0`
   * `set radio 916.75,500.0,5,5,0x16`
   * `serial mode kiss`

### On the first machine

 * `sudo tncattach --mtu=230 -e -noipv6 --ipv4=10.10.10.10/24 /dev/ttyACM0 115200`

### On the second machine

 * `sudo tncattach --mtu=230 -e -noipv6 --ipv4=10.10.10.11/24 /dev/ttyACM0 115200`
