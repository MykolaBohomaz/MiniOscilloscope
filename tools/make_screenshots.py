#!/usr/bin/env python3
"""Build tools/render_preview and turn its PBM frames into README images.

    python3 tools/make_screenshots.py            # -> docs/img/*.png

The frames are drawn by the unmodified firmware UI code fed with a simulated
signal (see tools/render_preview.c); they are not photographs of hardware.
"""
import pathlib
import subprocess
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[1]
SCALE = 4
BG, FG = (222, 228, 214), (24, 34, 48)


def lcd_png(pbm: pathlib.Path, png: pathlib.Path) -> None:
    src = Image.open(pbm).convert("1")
    w, h = src.size
    out = Image.new("RGB", (w * SCALE + 16, h * SCALE + 16), BG)
    px = out.load()
    for y in range(h):
        for x in range(w):
            if src.getpixel((x, y)) == 0:          # PBM: 1 = black -> PIL 0
                for dy in range(SCALE):
                    for dx in range(SCALE):
                        px[8 + x * SCALE + dx, 8 + y * SCALE + dy] = FG
    out.save(png)
    print("wrote", png.relative_to(ROOT))


def main() -> None:
    build = ROOT / "build-tests"
    subprocess.run(["cmake", "-S", ROOT / "tests", "-B", build, "-DSCOPE_SANITIZE=OFF"], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", build, "--target", "render_preview"], check=True,
                   stdout=subprocess.DEVNULL)
    dst = ROOT / "docs" / "img"
    dst.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([build / "render_preview", tmp], check=True, stdout=subprocess.DEVNULL)
        for pbm in sorted(pathlib.Path(tmp).glob("*.pbm")):
            lcd_png(pbm, dst / (pbm.stem + ".png"))


if __name__ == "__main__":
    main()
