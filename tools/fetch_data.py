import sys
import usb.core
import usb.util
import struct

def fetch_data(cmd, out_path):
    dev = usb.core.find(idVendor=0x18d1, idProduct=0xd00d)
    if not dev:
        print("Device not found")
        sys.exit(1)

    cfg = dev.get_active_configuration()
    intf = cfg[(0,0)]
    usb.util.claim_interface(dev, intf)
    try:
        ep_out = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        ep_in = usb.util.find_descriptor(intf, custom_match=lambda e: usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

        ep_out.write(cmd.encode())
        resp = bytes(ep_in.read(512, timeout=2000)).decode('latin1', errors='replace')
        print(f"Command '{cmd}' response: {resp}")

        ep_out.write(b'upload')
        resp = bytes(ep_in.read(512, timeout=2000)).decode('latin1', errors='replace')
        print(f"Upload response: {resp}")
        if resp.startswith('DATA'):
            total = int(resp[4:12], 16)
            print(f"Receiving {total} bytes...")
            data = bytearray()
            while len(data) < total:
                chunk = ep_in.read(min(total - len(data), 65536), timeout=5000)
                data.extend(chunk)
            final_resp = bytes(ep_in.read(512, timeout=2000)).decode('latin1', errors='replace')
            print(f"Final response: {final_resp}")
            with open(out_path, 'wb') as f:
                f.write(data)
            print(f"Saved {len(data)} bytes to {out_path}")
        else:
            print("Upload failed, unexpected response:", resp)
    finally:
        usb.util.release_interface(dev, intf)

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'oem screenshot'
    out_path = sys.argv[2] if len(sys.argv) > 2 else '/tmp/screen_after.raw'
    fetch_data(cmd, out_path)
