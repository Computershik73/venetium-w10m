"""Tile, store and splash images for the package: Venetium's logo, drawn here.

Usage: make-assets.py <Assets dir>

The logo is Chromium's wheel in one colour: three segments around a centre
circle in Chromium's own proportions (the centre 0.406 of the radius, the
band around it out to 0.5, each segment's edge tangent to the band), with
the band running on between the segments as a gap of its own width, and a V
cut out of the centre. It is white on transparent ground, so the tile colour
(BackgroundColor in the manifest, or the accent) shows through; the store
logo has the tile colour under it.

Every image is drawn at its own size (no scaling of a bigger picture), at
scale 100, 200 and 400: phones' displays are scale 200 to 400.

The images in app/VenetiumW10m/Assets since 222.69 are not this script's:
they were made from this logo with Visual Studio's visual-assets generator
(scales 100 to 400, the app list's target sizes, tiles named SmallTile and
LargeTile as the manifest has them). Running this replaces them with its
own layout.
"""
import os
import sys

import numpy as np
from PIL import Image

HUB, BAND = 0.406, 0.5
TILE = (0x1C, 0x1B, 0x22)

# name: (width, height, the wheel's diameter as a share of the shorter side,
#        its centre moved up by this share of the height, opaque ground)
SIZES = {
    "Square44x44Logo": (44, 44, 0.72, 0.0, None),
    "SmallTile": (71, 71, 0.62, 0.0, None),
    # The tile's name is written along its bottom.
    "Square150x150Logo": (150, 150, 0.54, 0.04, None),
    "Wide310x150Logo": (310, 150, 0.54, 0.04, None),
    "LargeTile": (310, 310, 0.5, 0.0, None),
    "StoreLogo": (50, 50, 0.8, 0.0, TILE),
    "SplashScreen": (620, 300, 0.55, 0.0, None),
}
SCALES = (1, 2, 4)


def capsule(x, y, ax, ay, bx, by, w):
    """A stroke from a to b, round at the low end, cut level at the top."""
    dx, dy = bx - ax, by - ay
    t = np.clip(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0, 1)
    inside = np.hypot(x - (ax + t * dx), y - (ay + t * dy)) <= w / 2
    return inside & (y <= max(ay, by))


def logo(diameter):
    """The logo's coverage (0..1) on a square of the given diameter."""
    ss = 8 if diameter < 128 else 4
    n = int(round(diameter)) * ss
    r = n / 2
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    x = (xx + 0.5 - r) / r
    y = -(yy + 0.5 - r) / r
    del xx, yy
    d = np.hypot(x, y)

    wheel = (d <= 1.0) & (d > BAND)
    for k in range(3):
        # The band runs out along the tangent at its top, then 120 and 240
        # degrees on, as Chromium's segment edges do.
        th = np.radians(90 + 120 * k)
        u = th - np.pi / 2
        across = x * np.cos(th) + y * np.sin(th)
        along = x * np.cos(u) + y * np.sin(u)
        wheel &= ~((across >= HUB) & (across <= BAND) & (along >= 0))

    # The V: a short left arm, a long right arm, its tips level.
    s, w, cx = 0.36, 0.38 * 0.36, -0.015
    left_top = (cx - 0.55 * s, 0.50 * s)
    bottom = (cx - 0.02 * s, -0.58 * s)
    right_top = (cx + 0.66 * s, 0.62 * s)
    v = (capsule(x, y, *left_top, *bottom, w) |
         capsule(x, y, *bottom, *right_top, w))
    centre = (d <= HUB) & ~v

    m = (wheel | centre).astype(np.float32)
    side = n // ss
    return m.reshape(side, ss, side, ss).mean(axis=(1, 3))


def make(width, height, share, lift, ground):
    canvas = np.zeros((height, width), np.float32)
    side = int(round(min(width, height) * share))
    mark = logo(side)
    left = (width - side) // 2
    top = int(round((height - side) / 2 - lift * height))
    canvas[top:top + side, left:left + side] = mark
    alpha = (canvas * 255 + 0.5).astype(np.uint8)
    if ground is None:
        white = np.full((height, width, 3), 255, np.uint8)
        return Image.fromarray(np.dstack([white, alpha]), "RGBA")
    g = np.array(ground, np.float32)
    rgb = g + (255 - g) * canvas[..., None]
    return Image.fromarray((rgb + 0.5).astype(np.uint8), "RGB")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    count = 0
    for name, (w, h, share, lift, ground) in SIZES.items():
        for scale in SCALES:
            suffix = ".png" if scale == 1 else ".scale-%d.png" % (scale * 100)
            make(w * scale, h * scale, share, lift, ground).save(
                os.path.join(out, name + suffix), optimize=True)
            count += 1
    print("assets: %d images in %s" % (count, out))


if __name__ == "__main__":
    main()
