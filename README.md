## lorapipe

[![experimental](http://badges.github.io/stability-badges/dist/experimental.svg)](http://github.com/badges/stability-badges)[![license](https://img.shields.io/github/license/datapartyjs/lorapipe)](https://github.com/datapartyjs/lorapipe/blob/master/license.txt)

Lorapipe is a tool for piping LoRa data to and from consumer grade radios.

 * Code - [github.com/datapartyjs/lorapipe](https://github.com/datapartyjs/lorapipe)
 * Releases - [github.com/datapartyjs/lorapipe/releases](https://github.com/datapartyjs/lorapipe/releases)
 * Support - [ko-fi/dataparty](https://ko-fi.com/dataparty)

## Features

 - Simple serial cli built into the firmware
 - Transmit raw bytes (hex) over LoRA using serial CLI
 - LoRA packet logging to serial (hex)
 - KISS-TNC mode

## Compiling
- Install [PlatformIO](https://docs.platformio.org) in [Visual Studio Code](https://code.visualstudio.com).
- Clone and open the lorapipe repository in Visual Studio Code.
- See the example applications you can modify and run:
  - [Simple Repeater](./examples/simple_repeater)

## Flashing

Download precompiled firmware releases - [github.com/datapartyjs/lorapipe/releases](https://github.com/datapartyjs/lorapipe/releases)

We haven't built a flashing tool yet. You can flash builds using the meshcore flasher, the OEM provided flashing tools or using the developer instructions to flash using VS Code.

- Flash using your platform's OEM flashing tool
- Use the meshcore flasher - http://flasher.meshcore.co.uk/


## Hardware Compatibility

lorapipe is designed for devices supported by MeshCore so check their support list in the [MeshCore Flasher](https://flasher.meshcore.co.uk). We support most of the same hardware see [variants](https://github.com/datapartyjs/lorapipe/tree/main/variants).

## Serial CLI

The lorapipe firmware initially starts up in a serial mode which is human readable. You can connect to the serial console using a serial terminal application. You may have to configure the baud rate, which defaults to `115200`. The following command uses `minicom` to connect to the serial port, please replace `/dev/ttyACM0` with the proper device node for your serial port:

`minicom -b 115200 -D /dev/ttyACM0`

It may be helpful on Linux systems to run the following command to set the default baud when interacting with the serial port:

`stty -F /dev/ttyACM0 115200`

Once connected, the MeshTNC device has a simple CLI. The CLI is largely similar to MeshCore with a few notable additions.


### CLI additions

 * `txraw <hex...>` - Transmist a packet
 * `get syncword <word>` - Read the syncword setting
 * `set kiss port <port>` - Set the KISS device port
 * `set radio <freq>,<bw>,<sf>,<coding-rate>,<syncword>` - Configure the radio
 * `serial mode kiss` - Switch to KISS mode
 * `rxlog on`
   * Output format: ` [timestamp],[type=RXLOG],[rssi],[snr],[hex...]\n` 
 * `set`/`get txpower` - MeshCore's `set`/`get tx` has been renamed appropriately

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
 * `set freq`
 * `erase`
 * `ver`
 * `log start`
 * `log stop`
 * `rxlog on`
 * `rxlog off`
</details>

## KISS Mode

KISS mode allows for operating the LoRA radio as a KISS modem, which makes it compatible with a lot of pre-existing radio software, including APRS software, and the Linux kernel.

### Enabling KISS Mode
 * Open a serial console and connect to the lorapipe device
 * `serial mode kiss`

### Exiting KISS Mode
 * To exit KISS mode and return to CLI mode, you can send a KISS exit sequence like so: `echo -ne '\xC0\xFF\xC0' > /dev/ttyUSBx`
   * For this to work, ensure your serial port's settings and baud rate is set correctly with `stty`

## APRS over LoRa

<img src="https://github.com/user-attachments/assets/ca4e8caf-5eff-44d3-8ff0-c9d57bfc6ca3" width="40%"></img> <img src="https://github.com/user-attachments/assets/aa4506dd-34b6-4277-af8e-3470ef8f8dfa" width="40%"></img> 

You can use your favorite APRS tools with lorapipe. Simply select a frequency, place the radio into kiss mode and connect to your APRS tools as a KISS TNC device.

 * `minicom -D /dev/ttyACM0`
   * `set radio 918.25,500.0,7,5,0x16`
   * `serial mode kiss`

### APRS Software

lorapipe should work with lots of APRS clients, we've tested on the following:

 * [xastir](https://xastir.org/index.php/Main_Page)
 * [APRSisce32](http://aprsisce.wikidot.com/)

## AX.25 over LoRA

There is a wealth of knowledge and examples available here: https://linux-ax25.in-berlin.de/wiki/AX.25

There's a lot of very interesting things that can be done directly over AX.25 without involving IP, but the following example will demonstrate how to assign an IP address to the AX.25 interface. This can be done on two different machines with two MeshTNC's, and you should be able to ping between them over the air, as long as the radio settings on the MeshTNC's match, and the IP addresses assigned are unique, and in the same subnet. IP routing will also work if configured properly!

### AX.25 Quick Start

* Attach the MeshTNC to a Linux system.
* Assign the proper radio settings on the MeshTNC CLI
* Enter KISS mode on the MeshTNC CLI
  * After entering KISS mode, please exit your terminal program to release the serial port.
* On the attached Linux system, edit `/etc/ax25/axports`:
  * Add the following line:
    * `0               AL1CE-1         115200  220     2       AX25 test`
    * Replace `AL1CE-1` with your callsign and SSID
* Attach the MeshTNC to the AX.25 port using: `kissattach /dev/ttyACM0 0`
  * After running this command, check to see what interface it created: `sudo dmesg | grep mkiss`
    * `[88447.885556] mkiss: ax0: crc mode is auto.`
* You can assign an IP address by running: `ip addr add 10.10.10.10/24 dev ax0`

## Ethernet over LoRa

<img src="https://github.com/user-attachments/assets/d8347c1c-d76d-469b-89d2-cd2c18859607" width="40%"></img>


Run the following on two or more computers, each with a lorapipe device attached, to create an ethernet over LoRa network.

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
