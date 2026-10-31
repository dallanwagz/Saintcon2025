#!/usr/bin/env python3
"""Reflash helper for a badge running USB HID.

While TinyUSB owns the USB port the usual USB-Serial/JTAG port is gone. Opening the badge's CDC console
port at 1200 baud makes the firmware hand the PHY back and reboot into the ROM download mode, where the
USB-Serial/JTAG port (303A:1001) reappears. Then flash as usual:

    python tools/enter_bootloader.py /dev/cu.usbmodemXXXX    # the CDC port
    idf.py -p /dev/cu.usbmodemYYYY flash                     # the port it prints

Run it with the IDF Python env (it has pyserial). Fallback with no working CDC: hold the joystick centre
while powering on, which keeps the badge on USB-Serial/JTAG for that boot.
"""
import glob
import sys
import time

import serial

if len(sys.argv) != 2:
    sys.exit(__doc__)
cdc = sys.argv[1]
before = set(glob.glob("/dev/cu.usbmodem*"))
serial.Serial(cdc, 1200).close()
print(f"touched {cdc} at 1200 baud, waiting for the download-mode port...")
deadline = time.time() + 12
while time.time() < deadline:
    now = set(glob.glob("/dev/cu.usbmodem*"))
    new = sorted(now - before)
    if new:
        print(f"download mode on {new[0]}")
        sys.exit(0)
    time.sleep(0.25)
sys.exit("no new port appeared; the badge may need the centre-button boot or JP1")
