"""Compare uncompressed 24/32-bit BMP images in their encoded RGB space."""

import argparse
import math
import struct
from pathlib import Path


def read_bmp(path):
    data = Path(path).read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path}: invalid BMP header")
    offset = struct.unpack_from("<I", data, 10)[0]
    header = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    planes, bits, compression = struct.unpack_from("<HHI", data, 26)
    if header < 40 or width <= 0 or height == 0 or planes != 1 or bits not in (24, 32) or compression != 0:
        raise ValueError(f"{path}: expected uncompressed 24/32-bit BMP")
    stride = ((width * bits + 31) // 32) * 4
    if offset + stride * abs(height) > len(data):
        raise ValueError(f"{path}: truncated BMP")
    pixels = []
    for y in range(abs(height)):
        source_y = y if height < 0 else abs(height) - 1 - y
        row = offset + source_y * stride
        for x in range(width):
            pixels.extend(data[row + x * (bits // 8):row + x * (bits // 8) + 3])
    return (width, abs(height)), pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference")
    parser.add_argument("images", nargs="+")
    args = parser.parse_args()
    size, reference = read_bmp(args.reference)
    for path in args.images:
        image_size, image = read_bmp(path)
        if image_size != size:
            raise ValueError(f"{path}: image dimensions differ from reference")
        errors = [a - b for a, b in zip(image, reference)]
        mse = sum(value * value for value in errors) / len(errors)
        mae = sum(abs(value) for value in errors) / len(errors)
        print(f"{path}: {size[0]}x{size[1]} MSE={mse:.6f} MAE={mae:.6f} RMSE={math.sqrt(mse):.6f}")


if __name__ == "__main__":
    main()
