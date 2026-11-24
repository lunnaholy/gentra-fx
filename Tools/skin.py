# Готовит фото кузова без колёс (PNG с прозрачностью, нос вправо) под car_<цвет>.png:
# убирает тень и ошмётки вокруг, режет по кузову, ставит низ картинки на асфальт
# и печатает колёса для Geo в Source/RoadView.cpp. Огни (head, tail, side, exhaust) — на глаз.
#   python3 Tools/skin.py фото.png Assets/car_<цвет>.png
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert('RGBA')
a = np.array(im)[..., 3]

# Кузов — самый большой непрозрачный кусок, заливкой от середины самой плотной строки.
mask = Image.fromarray(((a > 128) * 255).astype('uint8')).copy()
ImageDraw.floodfill(mask, (a.shape[1] // 2, int(np.argmax((a > 128).sum(1)))), 128)
keep = np.array(mask) == 128
# Дырки внутри силуэта (стёкла пожиже) — тоже кузов: снаружи только то, что достаёт до угла картинки.
outside = Image.fromarray(np.where(keep, 0, 255).astype('uint8')).copy()
ImageDraw.floodfill(outside, (0, 0), 64)
keep = np.array(outside) != 64
near = np.array(Image.fromarray((keep * 255).astype('uint8')).filter(ImageFilter.MaxFilter(5))) > 0
arr = np.array(im)
arr[..., 3] = np.where(near, a, 0) # полупрозрачные края и стёкла у кузова остаются
x0, y0, x1, y1 = Image.fromarray((keep * 255).astype('uint8')).getbbox()
body, k = Image.fromarray(arr).crop((x0, y0, x1, y1)), keep[y0:y1, x0:x1]
H, W = k.shape

# Арки: где нижний край кузова заметно выше порога.
bottom = np.array([H - 1 - np.argmax(k[::-1, x]) if k[:, x].any() else -1 for x in range(W)])
sill = np.percentile(bottom[bottom > 0], 90)
deep, open_ = bottom < sill - H * 0.12, bottom < sill - H * 0.03
arches, x = [], 0
while x < W:
    if deep[x]:
        s = x
        while x < W and deep[x]: x += 1
        if x - s > W * 0.05:
            l, r = s, x - 1
            while l > 0 and open_[l - 1]: l -= 1
            while r < W - 1 and open_[r + 1]: r += 1
            # центр — по глубокой части (пологий бампер не тянет арку вбок), радиус — до ближнего края
            c = (s + x - 1) / 2
            arches.append((c, min(c - l, r - c), bottom[s:x].min()))
    x += 1
assert len(arches) == 2, f'нашлось арок: {len(arches)}'

# Колесо: вписано в арку, зазор сверху как у старого кузова; низ колеса = низ картинки.
R = sum(h for _, h, _ in arches) / 2
cy = max(t for *_, t in arches) + 0.0095 * W + R
outH = int(round(max(H, cy + R)))
canvas = Image.new('RGBA', (W, outH))
canvas.paste(body, (0, 0))
canvas.resize((1024, round(outH * 1024 / W)), Image.LANCZOS).save(dst)

u = lambda v: f'{v / W:.4f}f'
print(f'{{ {u(arches[0][0])}, {u(arches[1][0])}, {u(outH - cy)}, {u(R)}, <head>, <tail>, <side>, <exhaust> }}')
