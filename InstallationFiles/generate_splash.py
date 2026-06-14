#!/usr/bin/env python3
"""Generate a professional 800x480 splash screen for OSCAR-100 WB Monitor.

Output:
  data/splash.jpg
"""

from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math
import random

W, H = 800, 480


def load_font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = []
    if bold:
        candidates += [
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
            "/System/Library/Fonts/Supplemental/Helvetica.ttc",
        ]
    candidates += [
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Menlo.ttc",
    ]
    for p in candidates:
        try:
            return ImageFont.truetype(p, size)
        except Exception:
            continue
    return ImageFont.load_default()


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def make_background() -> Image.Image:
    img = Image.new("RGB", (W, H), "#0a0d14")
    px = img.load()

    top = (7, 16, 31)
    mid = (11, 28, 52)
    bot = (8, 12, 20)

    for y in range(H):
        t = y / (H - 1)
        if t < 0.6:
            k = t / 0.6
            r = int(lerp(top[0], mid[0], k))
            g = int(lerp(top[1], mid[1], k))
            b = int(lerp(top[2], mid[2], k))
        else:
            k = (t - 0.6) / 0.4
            r = int(lerp(mid[0], bot[0], k))
            g = int(lerp(mid[1], bot[1], k))
            b = int(lerp(mid[2], bot[2], k))

        for x in range(W):
            px[x, y] = (r, g, b)

    return img


def add_glow(img: Image.Image) -> Image.Image:
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    d.ellipse((460, 50, 930, 470), fill=(0, 180, 255, 52))
    d.ellipse((-140, 180, 280, 600), fill=(0, 255, 170, 38))
    d.ellipse((250, -170, 620, 180), fill=(70, 120, 255, 26))

    layer = layer.filter(ImageFilter.GaussianBlur(60))
    return Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")


def add_grid(img: Image.Image) -> None:
    d = ImageDraw.Draw(img)
    grid_color = (24, 62, 96)

    for x in range(0, W, 32):
        d.line((x, 0, x, H), fill=grid_color, width=1)
    for y in range(0, H, 24):
        d.line((0, y, W, y), fill=grid_color, width=1)

    d.line((0, 25, W, 25), fill=(42, 110, 170), width=2)
    d.line((0, 335, W, 335), fill=(38, 78, 118), width=1)
    d.line((0, 436, W, 436), fill=(35, 70, 105), width=1)


def add_spectrum_theme(img: Image.Image) -> None:
    d = ImageDraw.Draw(img)

    left = 44
    right = 798
    top = 65
    bottom = 300

    # Spectrum envelope
    points = []
    for x in range(left, right + 1, 3):
        t = (x - left) / (right - left)
        y = (
            215
            + 30 * math.sin(t * 16.0)
            + 16 * math.sin(t * 49.0)
            + 10 * math.cos(t * 71.0)
        )

        # Beacon bump near left side
        y -= 80 * math.exp(-((t - 0.16) ** 2) / 0.0012)

        # DATV peaks
        y -= 40 * math.exp(-((t - 0.47) ** 2) / 0.0025)
        y -= 48 * math.exp(-((t - 0.70) ** 2) / 0.0021)
        y = max(top, min(bottom, int(y)))
        points.append((x, y))

    fill_poly = points + [(right, bottom), (left, bottom)]
    d.polygon(fill_poly, fill=(8, 108, 86))
    d.line(points, fill=(90, 255, 145), width=2)

    # Waterfall strip
    wf_top, wf_h = 336, 100
    random.seed(100)
    for y in range(wf_top, wf_top + wf_h):
        for x in range(left, right, 2):
            t = (x - left) / (right - left)
            band = 0.5 + 0.5 * math.sin((y * 0.22) + t * 40.0)
            peak = (
                0.9 * math.exp(-((t - 0.16) ** 2) / 0.002)
                + 0.8 * math.exp(-((t - 0.47) ** 2) / 0.003)
                + 1.0 * math.exp(-((t - 0.70) ** 2) / 0.003)
            )
            v = min(1.0, max(0.0, 0.35 * band + 0.7 * peak + random.uniform(-0.08, 0.08)))
            if v < 0.33:
                c = (0, int(40 + v * 80), int(120 + v * 220))
            elif v < 0.66:
                c = (int(20 + v * 120), int(120 + v * 180), int(220 - v * 120))
            else:
                c = (255, int(220 - (v - 0.66) * 300), int(40 + (1 - v) * 90))
            d.line((x, y, x + 1, y), fill=c, width=1)

    # Bandplan hints
    d.rectangle((left, 448, left + 145, 474), outline=(180, 70, 70), width=1)
    d.rectangle((left + 170, 448, left + 470, 474), outline=(70, 140, 255), width=1)
    d.rectangle((left + 490, 448, right, 474), outline=(70, 210, 130), width=1)


def add_text(img: Image.Image) -> None:
    d = ImageDraw.Draw(img)
    title = load_font(52, bold=True)
    subtitle = load_font(24, bold=False)
    small = load_font(18, bold=False)
    micro = load_font(14, bold=False)

    # Soft shadow for title
    d.text((46, 36), "OSCAR-100", font=title, fill=(0, 0, 0))
    d.text((44, 34), "OSCAR-100", font=title, fill=(226, 244, 255))

    d.text((46, 96), "Wideband Monitor", font=subtitle, fill=(122, 206, 255))
    d.text((46, 126), "Spectrum  |  BATC Chat  |  Weather", font=small, fill=(118, 171, 210))

    chip = "ESP32-S3  •  800x480 RGB  •  GT911 Touch"
    tw = d.textbbox((0, 0), chip, font=small)[2]
    d.rounded_rectangle((W - tw - 36, 28, W - 20, 56), radius=12, fill=(15, 36, 66), outline=(45, 120, 180), width=1)
    d.text((W - tw - 26, 35), chip, font=small, fill=(180, 220, 245))

    d.text((46, 455), "HB9IIU Firmware", font=micro, fill=(100, 135, 168))
    d.text((W - 220, 455), "booting interfaces...", font=micro, fill=(100, 170, 130))


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    out_dir = root / "data"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "splash.jpg"

    img = make_background()
    img = add_glow(img)
    add_grid(img)
    add_spectrum_theme(img)
    add_text(img)

    img.save(out_path, format="JPEG", quality=93, optimize=True, progressive=True)
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    main()
