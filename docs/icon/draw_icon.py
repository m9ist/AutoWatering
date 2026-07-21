# Иконка группы автополива: зелёная плата с чипом + капля воды. 1024x1024 PNG.
from PIL import Image, ImageDraw

S = 4  # суперсэмплинг против лесенок
W = 1024 * S
img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
d = ImageDraw.Draw(img)


def px(v):  # координаты в "финальных" пикселях -> внутренние
    return int(v * S)


# --- фон: тёмный круг (телега режет в круг — рисуем сразу баджем) ---
d.ellipse([0, 0, W, W], fill=(15, 32, 46, 255))
d.ellipse([px(18), px(18), W - px(18), W - px(18)], outline=(38, 66, 88, 255), width=px(10))

# --- плата ---
bx0, by0, bx1, by1 = px(212), px(232), px(812), px(832)
d.rounded_rectangle([bx0, by0, bx1, by1], radius=px(48),
                    fill=(46, 125, 79, 255), outline=(27, 80, 50, 255), width=px(10))

# крепёжные отверстия
for cx, cy in [(272, 292), (752, 292), (272, 772), (752, 772)]:
    d.ellipse([px(cx - 17), px(cy - 17), px(cx + 17), px(cy + 17)],
              fill=(15, 32, 46, 255), outline=(212, 175, 55, 255), width=px(7))

# --- дорожки с площадками ---
trace = (120, 200, 150, 255)
tw = px(9)


def pad(x, y):
    d.ellipse([px(x - 13), px(y - 13), px(x + 13), px(y + 13)], fill=trace)
    d.ellipse([px(x - 6), px(y - 6), px(x + 6), px(y + 6)], fill=(46, 125, 79, 255))


def line(pts):
    d.line([(px(x), px(y)) for x, y in pts], fill=trace, width=tw, joint="curve")


line([(512, 352), (512, 300)]); pad(512, 300)
line([(432, 432), (352, 432), (352, 340)]); pad(352, 340)
line([(592, 432), (688, 432), (688, 356)]); pad(688, 356)
line([(432, 592), (348, 592), (348, 700)]); pad(348, 700)
line([(592, 560), (680, 560), (680, 640)]); pad(680, 640)
line([(472, 672), (472, 736), (400, 736)]); pad(400, 736)

# --- чип по центру ---
cx0, cy0, cx1, cy1 = px(432), px(432), px(592), px(592)
# ножки
leg = (208, 208, 208, 255)
for i in range(4):
    off = px(452 + i * 32)
    d.rectangle([off, cy0 - px(26), off + px(16), cy0], fill=leg)            # верх
    d.rectangle([off, cy1, off + px(16), cy1 + px(26)], fill=leg)            # низ
    d.rectangle([cx0 - px(26), off, cx0, off + px(16)], fill=leg)            # лево
    d.rectangle([cx1, off, cx1 + px(26), off + px(16)], fill=leg)            # право
d.rounded_rectangle([cx0, cy0, cx1, cy1], radius=px(14),
                    fill=(24, 26, 30, 255), outline=(60, 64, 70, 255), width=px(6))
d.ellipse([px(448), px(448), px(472), px(472)], fill=(120, 124, 130, 255))  # метка 1-го пина

# --- гребёнка пинов снизу платы ---
for i in range(8):
    x = 296 + i * 62
    d.rectangle([px(x), px(796), px(x + 26), px(832)], fill=(212, 175, 55, 255))

# --- капля воды (перекрывает угол платы) ---
dcx, dcy, r = 740, 700, 128            # центр "тела" капли


def drop(cx, cy, radius, color):
    apex_y = cy - radius * 2.1
    d.polygon([(px(cx), px(apex_y)),
               (px(cx - radius + 6), px(cy - 40)),
               (px(cx + radius - 6), px(cy - 40))], fill=color)
    d.ellipse([px(cx - radius), px(cy - radius), px(cx + radius), px(cy + radius)], fill=color)


# тёмный контур всей формы, чтобы капля читалась на плате
drop(dcx, dcy, r + 16, (15, 32, 46, 255))
drop(dcx, dcy, r, (64, 170, 230, 255))
# блик
d.ellipse([px(dcx - 78), px(dcy - 42), px(dcx - 28), px(dcy + 42)], fill=(200, 235, 255, 200))

out = img.resize((1024, 1024), Image.LANCZOS)
out.save("aw_icon.png")  # запускать из docs/icon: python draw_icon.py
print("done")
