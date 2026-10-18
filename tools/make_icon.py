"""Draws BeatForge's app icon: app/Resources/icon.png (1024 x 1024) and
packaging/windows/BeatForge.ico (16-256 px, for the installer).

The header logo, large: six rounded bars alternating purple and pink on the
app's dark background (a rounded square). Run: python3 tools/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

SIZE, SCALE = 1024, 4              # draw 4x larger, then downsample (smooth edges)
BG, BORDER = (11, 16, 34), (38, 47, 94)          # bgPanel, border
PURPLE, PINK = (168, 85, 247), (236, 72, 153)    # accent purple, pink
HEIGHTS = [8, 16, 26, 14, 22, 10]  # the logo's bar heights (out of 26)

big = SIZE * SCALE
img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

margin = int(big * 0.06)
d.rounded_rectangle([margin, margin, big - margin, big - margin], radius=int(big * 0.2),
                    fill=BG, outline=BORDER, width=int(big * 0.012))

bar_w, pitch = big * 0.075, big * 0.125
total = pitch * 5 + bar_w
x0, cy, full = (big - total) / 2, big / 2, big * 0.56
for i, h in enumerate(HEIGHTS):
    hh = full * h / 26
    x = x0 + i * pitch
    d.rounded_rectangle([x, cy - hh / 2, x + bar_w, cy + hh / 2], radius=bar_w / 2,
                        fill=PURPLE if i % 2 == 0 else PINK)

root = Path(__file__).resolve().parent.parent
icon = img.resize((SIZE, SIZE), Image.LANCZOS)
icon.save(root / "app" / "Resources" / "icon.png")
(root / "packaging" / "windows").mkdir(parents=True, exist_ok=True)
icon.save(root / "packaging" / "windows" / "BeatForge.ico",
          sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("icon.png and BeatForge.ico written")
