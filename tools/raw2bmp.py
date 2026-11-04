import struct
import sys

def raw_to_bmp(raw_path, bmp_path, width=1080, height=2400):
    with open(raw_path, 'rb') as f:
        raw = f.read()
    
    # 32bpp BGRA
    row_bytes = width * 4
    image_size = row_bytes * height
    file_size = 54 + image_size

    # BMP Header
    bmp_header = struct.pack(
        '<2sIHHI',
        b'BM',
        file_size,
        0, 0,
        54
    )
    # DIB Header (BITMAPINFOHEADER)
    # Note: negative height means top-down bitmap!
    dib_header = struct.pack(
        '<IiiHHIIIIII',
        40,
        width,
        -height,
        1,
        32,
        0,
        image_size,
        2835, 2835, # 72 DPI
        0, 0
    )

    with open(bmp_path, 'wb') as f:
        f.write(bmp_header)
        f.write(dib_header)
        f.write(raw[:image_size])
    print(f"Saved {bmp_path} ({width}x{height})")

if __name__ == '__main__':
    raw_path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/screen.raw'
    bmp_path = sys.argv[2] if len(sys.argv) > 2 else '/tmp/screen.bmp'
    raw_to_bmp(raw_path, bmp_path)
