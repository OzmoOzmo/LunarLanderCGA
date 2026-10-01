"""Convert the original (or later colored) lander sheets to DOS frame files."""

import argparse
import struct
from pathlib import Path

from PIL import Image


OFF_CROPS = ((0, 2, 2), (3, 6, 3), (7, 10, 3), (11, 14, 3),
             (16, 19, 3), (21, 24, 3), (27, 29, 2))
ON_CROPS = ((0, 2, 5), (3, 6, 5), (7, 10, 5), (11, 15, 4),
            (16, 20, 3), (21, 26, 3), (27, 32, 2))


def convert(source, destination, crops, black_transparent):
    image = Image.open(source)
    if image.size != (280, 48):
        raise ValueError(f"{source}: expected a 280x48 sprite sheet")
    has_alpha = "A" in image.getbands() or "transparency" in image.info
    image = image.convert("RGBA")

    with destination.open("wb") as output:
        output.write(b"LLS1" + bytes((28,)))
        for quadrant in range(4):
            for left, right, bottom in crops:
                frame = image.crop((left * 8, 0, (right + 1) * 8,
                                    (bottom + 1) * 8))
                width, height = frame.size
                pivot_x, pivot_y = 12, 12
                if quadrant == 1:
                    frame = frame.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                    pivot_y = height - 1 - pivot_y
                elif quadrant == 2:
                    frame = frame.transpose(Image.Transpose.ROTATE_180)
                    pivot_x = width - 1 - pivot_x
                    pivot_y = height - 1 - pivot_y
                elif quadrant == 3:
                    frame = frame.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
                    pivot_x = width - 1 - pivot_x
                output.write(struct.pack("<BBBB", width, height, pivot_x, pivot_y))
                for red, green, blue, alpha in frame.getdata():
                    if alpha < 128 or (black_transparent and not has_alpha
                                       and (red, green, blue) == (0, 0, 0)):
                        output.write(b"\0")
                    else:
                        # 0 is transparent; 1..216 form a 6x6x6 RGB palette.
                        output.write(bytes((1 + (red * 5 // 255) * 36
                                            + (green * 5 // 255) * 6
                                            + blue * 5 // 255,)))
    print(f"{destination}: 28 frames from {source}")


def convert_moon(source, destination):
    image = Image.open(source).convert("RGBA")
    width, height = image.size
    with destination.open("wb") as output:
        output.write(b"MNS1")
        output.write(struct.pack("<HH", width, height))
        for red, green, blue, alpha in image.getdata():
            if alpha < 128:
                output.write(b"\0")
            else:
                output.write(bytes((1 + (red * 5 // 255) * 36
                                    + (green * 5 // 255) * 6
                                    + blue * 5 // 255,)))
    print(f"{destination}: moon sprite from {source}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--off", type=Path, default=Path("98.png"))
    parser.add_argument("--on", type=Path, default=Path("99.png"))
    parser.add_argument("--moon", type=Path, default=Path("97.png"))
    parser.add_argument("--output", type=Path, default=Path("."))
    parser.add_argument("--black-transparent", action="store_true",
                        help="Treat black as transparent even in RGBA artwork")
    options = parser.parse_args()
    options.output.mkdir(parents=True, exist_ok=True)
    convert(options.off, options.output / "LANDOFF.DAT", OFF_CROPS,
            options.black_transparent)
    convert(options.on, options.output / "LANDON.DAT", ON_CROPS,
            options.black_transparent)
    convert_moon(options.moon, options.output / "MOON.DAT")


if __name__ == "__main__":
    main()