"""
Quick test: send EMU_CMD_VERSION (0x01) to J-Link Pico Probe
and print the response.

Requires: pip install pyusb
On Windows, also needs libusb (WinUSB backend works via pyusb).
"""

import usb.core
import usb.util
import sys

VID = 0x1209
PID = 0xD0DA
EP_OUT = 0x01   # EP1 OUT (host → probe)
EP_IN  = 0x82   # EP2 IN  (probe → host)

def main():
    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        print(f"ERROR: Device {VID:04X}:{PID:04X} not found.")
        print("Check Device Manager and ensure WinUSB driver is assigned.")
        sys.exit(1)

    print(f"Found: {dev.manufacturer} - {dev.product} (S/N: {dev.serial_number})")

    # Ensure we can claim the interface
    if dev.is_kernel_driver_active(0):
        dev.detach_kernel_driver(0)
    dev.set_configuration()

    # --- Test 1: EMU_CMD_VERSION (0x01) ---
    print("\n--- EMU_CMD_VERSION (0x01) ---")
    dev.write(EP_OUT, bytes([0x01]))
    try:
        resp = dev.read(EP_IN, 512, timeout=2000)
        print(f"Response ({len(resp)} bytes): {bytes(resp).hex()}")
        # First 2 bytes = string length (LE), then ASCII string
        if len(resp) >= 2:
            str_len = resp[0] | (resp[1] << 8)
            if str_len > 0 and len(resp) >= 2 + str_len:
                version_str = bytes(resp[2:2+str_len]).decode('ascii', errors='replace')
                print(f"Version string: \"{version_str}\"")
    except usb.core.USBTimeoutError:
        print("TIMEOUT — no response received.")

    # --- Test 2: EMU_CMD_GET_HW_VERSION (0xF0) ---
    print("\n--- EMU_CMD_GET_HW_VERSION (0xF0) ---")
    dev.write(EP_OUT, bytes([0xF0]))
    try:
        resp = dev.read(EP_IN, 64, timeout=2000)
        print(f"Response ({len(resp)} bytes): {bytes(resp).hex()}")
        if len(resp) >= 4:
            hw_ver = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24)
            print(f"HW Version: {hw_ver}")
    except usb.core.USBTimeoutError:
        print("TIMEOUT — no response received.")

    # --- Test 3: EMU_CMD_GET_CAPS (0xE8) ---
    print("\n--- EMU_CMD_GET_CAPS (0xE8) ---")
    dev.write(EP_OUT, bytes([0xE8]))
    try:
        resp = dev.read(EP_IN, 64, timeout=2000)
        print(f"Response ({len(resp)} bytes): {bytes(resp).hex()}")
        if len(resp) >= 4:
            caps = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24)
            print(f"Capabilities: 0x{caps:08X}")
    except usb.core.USBTimeoutError:
        print("TIMEOUT — no response received.")

    print("\nDone.")

if __name__ == "__main__":
    main()
