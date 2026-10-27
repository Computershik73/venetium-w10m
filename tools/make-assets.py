"""Tile, store and splash images for the package, from Venetium's logo.

Usage: make-assets.py <logo.png> <Assets dir>

Each image is the logo centred on transparent ground at the share of the tile
Windows' design guidance gives a tile's icon (about two thirds), so the tile
colour (BackgroundColor in the manifest) shows around it. Scale-200 versions
are made too: a phone's display is scale 200 or more.

The images in app/VenetiumW10m/Assets since 222.63 are drawn by hand (a white
V on the tile colour) and are not made by this script.
"""
import os
import sys

from PIL import Image

SIZES = {
    "Square44x44Logo": (44, 44, 0.86),
    "Square71x71Logo": (71, 71, 0.6),
    "Square150x150Logo": (150, 150, 0.55),
    "Square310x310Logo": (310, 310, 0.5),
    "Wide310x150Logo": (310, 150, 0.6),
    "StoreLogo": (50, 50, 0.9),
    "SplashScreen": (620, 300, 0.45),
}


def make(logo, width, height, share):
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    side = int(min(width, height) * share)
    icon = logo.resize((side, side), Image.LANCZOS)
    canvas.paste(icon, ((width - side) // 2, (height - side) // 2), icon)
    return canvas


def main():
    logo = Image.open(sys.argv[1]).convert("RGBA")
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for name, (w, h, share) in SIZES.items():
        make(logo, w, h, share).save(os.path.join(out, name + ".png"))
        make(logo, w * 2, h * 2, share).save(
            os.path.join(out, name + ".scale-200.png"))
    print("assets: %d images in %s" % (2 * len(SIZES), out))


if __name__ == "__main__":
    main()
