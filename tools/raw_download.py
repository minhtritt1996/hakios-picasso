import sys
import time
import usb.core
import usb.util

def upload(payload_path):
    with open(payload_path, 'rb') as f:
        data = f.read()
    total = len(data)
    print(f"Uploading {payload_path}: {total} bytes (0x{total:x})")

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

        # Send download command
        cmd = f"download:{total:08x}".encode()
        ep_out.write(cmd)
        resp = bytes(ep_in.read(512, timeout=5000)).decode('latin1', errors='replace')
        print("Download cmd resp:", resp)
        if not resp.startswith("DATA"):
            print("Expected DATA, got:", resp)
            sys.exit(1)

        # Stream data in 64KB chunks
        chunk_size = 64 * 1024
        offset = 0
        t0 = time.time()
        while offset < total:
            chunk = data[offset:offset+chunk_size]
            ep_out.write(chunk, timeout=10000)
            offset += len(chunk)
            if offset % (2 * 1024 * 1024) == 0 or offset == total:
                print(f"Sent {offset}/{total} bytes ({offset*100//total}%)")

        # Read final response (OKAY)
        resp = bytes(ep_in.read(512, timeout=10000)).decode('latin1', errors='replace')
        dt = time.time() - t0
        print(f"Upload complete in {dt:.2f}s, response: {resp}")
        if not resp.startswith("OKAY"):
            print("Download failed:", resp)
            sys.exit(1)
    finally:
        usb.util.release_interface(dev, intf)

if __name__ == '__main__':
    upload(sys.argv[1] if len(sys.argv) > 1 else '/tmp/payload_metal_sm7250.bin')
