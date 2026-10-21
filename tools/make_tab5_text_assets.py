#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.9"
# dependencies = ["pillow>=10", "fonttools>=4.47"]
# ///
"""Builds the Tab5's text assets in src/tab5/assets/ from upstream fonts.

LVGL's built-in fonts cover ASCII only. The Tab5 UI chains them to:
  text.ttf   Montserrat Medium (same face as the built-ins): Latin-1,
             Latin Extended, Cyrillic, punctuation, currency
  more.ttf   Noto Sans Medium: Greek, Vietnamese, Cyrillic supplement,
             Armenian, Georgian, arrows, maths
  emoji<N>.bin  colour emoji from Noto Color Emoji, N px, one PNG each

Sources (all SIL OFL 1.1), downloaded by hand into one folder:
  Montserrat-Medium.ttf   github.com/JulietaUla/Montserrat  fonts/ttf/
  NotoSans-var.ttf        github.com/google/fonts  ofl/notosans/NotoSans[wdth,wght].ttf
  NotoColorEmoji-noflags.ttf  github.com/googlefonts/noto-emoji  2D/fonts/
  emoji-test.txt          unicode.org/Public/emoji/latest/

    uv run tools/make_tab5_text_assets.py <source-dir>

emoji<N>.bin layout (little-endian): "EMJ1", u32 count, then count entries of
(u32 codepoint, u32 offset, u32 length) sorted by codepoint, then PNG data;
offsets count from the start of the file.
"""

import io
import struct
import sys
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent / "src" / "tab5" / "assets"
EMOJI_SIZES = (26,)  # one size keeps flash in budget; it suits the body text
CBDT_STRIKE = 109  # the only bitmap size in Noto Color Emoji

TEXT_RANGES = [
    (0x00A0, 0x024F),  # Latin-1 Supplement, Latin Extended-A/B
    (0x0400, 0x04FF),  # Cyrillic
    (0x2010, 0x205E),  # general punctuation
    (0x20A0, 0x20C0),  # currency
    (0x2116, 0x2122),  # numero, trade mark
]
MORE_RANGES = [
    (0x0250, 0x02FF),  # IPA, modifier letters
    (0x0300, 0x036F),  # combining marks
    (0x0370, 0x03FF),  # Greek
    (0x0500, 0x052F),  # Cyrillic Supplement
    (0x0530, 0x058F),  # Armenian
    (0x10A0, 0x10FF),  # Georgian
    (0x1E00, 0x1EFF),  # Latin Extended Additional (Vietnamese)
    (0x1F00, 0x1FFF),  # Greek Extended
    (0x2190, 0x21FF),  # arrows
    (0x2200, 0x22FF),  # maths
    (0x25A0, 0x25FF),  # geometric shapes
]


def codepoints(ranges):
    return [cp for a, b in ranges for cp in range(a, b + 1)]


def subset_font(src, out, ranges, variation=None):
    font = TTFont(src)
    if variation:
        font = instancer.instantiateVariableFont(font, variation)
    opts = subset.Options()
    opts.layout_features = ["kern", "liga", "ccmp", "mark", "mkmk"]
    opts.hinting = False
    opts.name_IDs = [0, 1, 2, 3, 4, 5, 6]  # keep copyright and licence notices
    sub = subset.Subsetter(opts)
    sub.populate(unicodes=codepoints(ranges))
    sub.subset(font)
    font.save(out)
    print(f"{out.name}: {out.stat().st_size // 1024} KB")


def emoji_list(test_file):
    """Single-codepoint, fully-qualified emoji (an optional U+FE0F dropped)."""
    out = []
    group = ""
    for line in Path(test_file).read_text(encoding="utf-8").splitlines():
        if line.startswith("# group:"):
            group = line.split(":", 1)[1].strip()
            continue
        if not line or line.startswith("#") or "; fully-qualified" not in line:
            continue
        if group in ("Component", "Flags"):
            continue
        cps = [int(c, 16) for c in line.split(";")[0].split()]
        cps = [c for c in cps if c != 0xFE0F]
        if len(cps) == 1 and cps[0] >= 0x2000:  # leave ©, ® and digits to the text fonts
            out.append(cps[0])
    return sorted(set(out))


def render_emoji(font, cp):
    text = chr(cp)
    probe = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    l, t, r, b = probe.textbbox((0, 0), text, font=font, embedded_color=True)
    if r - l <= 0 or b - t <= 0:
        return None
    img = Image.new("RGBA", (r - l, b - t), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((-l, -t), text, font=font, embedded_color=True)
    if img.getbbox() is None:
        return None
    side = max(img.size)
    square = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    square.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
    return square


def pack(images, size, out):
    entries, blob = [], bytearray()
    header = 8 + 12 * len(images)
    for cp, img in images:
        buf = io.BytesIO()
        small = img.resize((size, size), Image.LANCZOS)
        small.save(buf, "PNG", optimize=True)
        data = buf.getvalue()
        # 256 colours with alpha is often smaller, with no visible loss at this size.
        buf = io.BytesIO()
        small.quantize(256, method=Image.Quantize.FASTOCTREE).save(buf, "PNG", optimize=True)
        if len(buf.getvalue()) < len(data):
            data = buf.getvalue()
        entries.append((cp, header + len(blob), len(data)))
        blob += data
    with open(out, "wb") as f:
        f.write(b"EMJ1" + struct.pack("<I", len(entries)))
        for e in entries:
            f.write(struct.pack("<III", *e))
        f.write(blob)
    print(f"{out.name}: {len(entries)} emoji, {out.stat().st_size // 1024} KB")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src = Path(sys.argv[1])
    OUT.mkdir(parents=True, exist_ok=True)
    subset_font(src / "Montserrat-Medium.ttf", OUT / "text.ttf", TEXT_RANGES)
    subset_font(src / "NotoSans-var.ttf", OUT / "more.ttf", MORE_RANGES, {"wght": 500, "wdth": 100})

    font = ImageFont.truetype(str(src / "NotoColorEmoji-noflags.ttf"), CBDT_STRIKE)
    images = []
    for cp in emoji_list(src / "emoji-test.txt"):
        img = render_emoji(font, cp)
        if img is not None:
            images.append((cp, img))
    for size in EMOJI_SIZES:
        pack(images, size, OUT / f"emoji{size}.bin")


if __name__ == "__main__":
    main()
