#!/usr/bin/env python3
"""Upload a firmware image to a MeshTNC over its serial port with the 'ota' CLI commands.

  upload_firmware.py --port /dev/ttyS0 --baud 921600 firmware.bin [--sig firmware.bin.sig]

By default the TNC is expected to be in KISS mode: each command goes as a data frame on KISS
port 1 (C0 10 <command> C0) and its reply comes back the same way; LoRa frames the TNC keeps
forwarding on port 0 in the meantime are skipped. With --mode cli the plain text CLI is used
instead. --enter-kiss switches a TNC that is in text mode into KISS mode first.

The image is sent as 'ota data <offset> <hex>' chunks, one at a time, each waiting for the
TNC's reply, so the TNC's flash writes pace the transfer and nothing can overflow its serial
buffer. A chunk whose reply is lost is sent again. 'ota end' makes the TNC check the signature
(when the firmware was built with OTA_PUBLIC_KEY) and the image, then reboot into it. After
the reboot the TNC is in text CLI mode again (KISS mode is not saved across reboots).

Needs pyserial: pip install pyserial
"""

import argparse
import re
import sys
import time

import serial

FEND, FESC, TFEND, TFESC = 0xC0, 0xDB, 0xDC, 0xDD
CLI_PORT_CMD = 0x10   # KISS port 1, data


class Link:
    def __init__(self, port, baud, mode, timeout):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.mode = mode
        self.timeout = timeout
        self.other_frames = 0   # KISS frames seen that weren't CLI replies (LoRa RX etc.)
        self.buf = bytearray()
        self.esc = False
        self.in_frame = False
        self.last_io = 0.0   # when the TNC last received or sent something

    def wake_if_idle(self):
        # A 'set powersave on' TNC light-sleeps in KISS mode once the serial link has been
        # idle for 200 ms, and bytes that arrive while it wakes up (about a millisecond: a
        # whole short frame at 921600 baud) are lost. So after any idle gap, a burst of empty
        # frames first, long enough to outlast the wake-up, then the real frame right behind
        # it while the TNC is still awake. Harmless when it was awake, or in text mode.
        if self.mode == "kiss" and time.time() - self.last_io > 0.15:
            self.ser.write(bytes([FEND] * 400))
            self.ser.flush()

    def send_text(self, line):
        # text CLI (no KISS framing), used by --enter-kiss and --verify
        self.ser.write(line.encode() + b"\r")
        self.ser.flush()

    def send(self, cmd):
        self.wake_if_idle()
        self.last_io = time.time()
        if self.mode == "kiss":
            frame = bytearray([FEND, CLI_PORT_CMD])
            for b in cmd.encode():
                if b == FEND:
                    frame += bytes([FESC, TFEND])
                elif b == FESC:
                    frame += bytes([FESC, TFESC])
                else:
                    frame.append(b)
            frame.append(FEND)
            self.ser.write(frame)
        else:
            self.ser.write(cmd.encode() + b"\r")
        self.ser.flush()

    def _kiss_reply(self, deadline):
        # returns the next port-1 data frame as text, or None on timeout
        while time.time() < deadline:
            data = self.ser.read(512)
            for b in data:
                if b == FEND:
                    if self.in_frame and len(self.buf) > 0:
                        frame = bytes(self.buf)
                        self.buf.clear()
                        self.esc = False
                        if frame[0] == CLI_PORT_CMD:
                            self.last_io = time.time()
                            return frame[1:].decode(errors="replace")
                        self.other_frames += 1
                    self.buf.clear()
                    self.esc = False
                    self.in_frame = True
                elif not self.in_frame:
                    continue   # noise between frames (e.g. boot messages)
                elif b == FESC:
                    self.esc = True
                elif self.esc:
                    self.buf.append(FEND if b == TFEND else FESC if b == TFESC else b)
                    self.esc = False
                else:
                    self.buf.append(b)
        return None

    def _cli_reply(self, deadline):
        # the text CLI echoes the command, then prints "  -> <reply>"; RXLOG lines may interleave
        line = bytearray()
        while time.time() < deadline:
            data = self.ser.read(512)
            for b in data:
                if b in (0x0A, 0x0D):
                    text = line.decode(errors="replace")
                    line.clear()
                    if text.startswith("  -> "):
                        self.last_io = time.time()
                        return text[5:]
                else:
                    line.append(b)
        return None

    def reply(self, timeout=None):
        deadline = time.time() + (timeout or self.timeout)
        return self._kiss_reply(deadline) if self.mode == "kiss" else self._cli_reply(deadline)

    def command(self, cmd, retries=3, timeout=None):
        for attempt in range(retries):
            self.send(cmd)
            r = self.reply(timeout)
            if r is not None:
                return r
        raise TimeoutError("no reply to '%s' after %d tries" % (cmd[:40], retries))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("firmware", help="firmware .bin (not the -merged.bin)")
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=921600, help="BYOMesh: 921600, other boards: 115200")
    ap.add_argument("--sig", help="signature file from sign_firmware.py (64 bytes)")
    ap.add_argument("--mode", choices=["kiss", "cli"], default="kiss", help="the TNC's current serial mode")
    ap.add_argument("--enter-kiss", action="store_true", help="TNC is in text mode: send 'serial mode kiss' first")
    ap.add_argument("--no-reboot", action="store_true", help="'ota end noreboot': apply, but let the TNC run on")
    ap.add_argument("--timeout", type=float, default=5.0, help="seconds to wait for each reply")
    ap.add_argument("--verify", action="store_true", help="after the reboot, read 'ver' from the new firmware")
    args = ap.parse_args()

    with open(args.firmware, "rb") as f:
        image = f.read()
    if len(image) < 32 or image[0] != 0xE9:
        sys.exit("%s doesn't start with the ESP32 image magic byte (use the plain firmware.bin)" % args.firmware)
    sig_hex = ""
    if args.sig:
        with open(args.sig, "rb") as f:
            sig = f.read()
        if len(sig) != 64:
            sys.exit("%s is not a 64 byte signature" % args.sig)
        sig_hex = sig.hex()

    link = Link(args.port, args.baud, "cli" if args.enter_kiss else args.mode, args.timeout)
    time.sleep(0.2)
    link.ser.reset_input_buffer()
    if args.enter_kiss:   # text mode: the TNC never sleeps there, no wake-up needed
        link.send_text("serial mode kiss")
        time.sleep(0.5)
        link.ser.reset_input_buffer()
        link.mode = "kiss"

    status = link.command("ota status")
    print("TNC: %s" % status)
    if "signature required" in status and not sig_hex:
        sys.exit("this firmware only accepts signed images: pass --sig")
    if "not supported" in status:
        sys.exit("the TNC can't be updated this way")

    r = link.command(("ota begin %d %s" % (len(image), sig_hex)).strip())
    print("TNC: %s" % r)
    if not r.startswith("OK"):
        sys.exit(1)
    m = re.search(r"up to (\d+) bytes", r)
    chunk = int(m.group(1)) if m else 128

    offset = 0
    started = time.time()
    last_shown = 0
    while offset < len(image):
        data = image[offset:offset + chunk]
        r = link.command("ota data %d %s" % (offset, data.hex()))
        if r.startswith("OK"):
            offset += len(data)
        else:
            m = re.search(r"expected offset (\d+)", r)
            if m and int(m.group(1)) <= len(image):
                offset = int(m.group(1))   # resynchronise after a lost reply
                continue
            link.command("ota abort")
            sys.exit("TNC: %s" % r)
        if time.time() - last_shown > 1 or offset == len(image):
            last_shown = time.time()
            pct = 100.0 * offset / len(image)
            rate = offset / max(time.time() - started, 0.001)
            sys.stdout.write("\r%7d / %d bytes (%.0f%%, %.1f kB/s)   " % (offset, len(image), pct, rate / 1000))
            sys.stdout.flush()
    print()

    r = link.command("ota end noreboot" if args.no_reboot else "ota end", timeout=max(args.timeout, 20))
    print("TNC: %s" % r)
    if link.other_frames:
        print("(%d other KISS frames received during the upload)" % link.other_frames)
    if not r.startswith("OK"):
        sys.exit(1)
    if args.no_reboot or not args.verify:
        return

    print("waiting for the TNC to reboot...")
    time.sleep(4)
    link.mode = "cli"   # a rebooted TNC is in text mode
    link.ser.reset_input_buffer()
    link.send_text("")
    time.sleep(0.2)
    link.ser.reset_input_buffer()
    try:
        print("new firmware: %s" % link.command("ver", timeout=5))
    except TimeoutError as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
