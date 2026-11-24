#!/usr/bin/env python3
"""Перегенерирует PNG-заглушки в Assets/ из Tools/assets.html через headless Chrome.

Свои картинки этим скриптом не трогайте: он перезапишет все файлы в Assets/.
Запуск: python3 Tools/make_assets.py [путь к Chrome]
"""
import base64
import html
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CANDIDATES = [
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    "google-chrome", "chromium", "chrome",
]


def find_chrome():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for c in CANDIDATES:
        if pathlib.Path(c).exists() or shutil.which(c):
            return c
    sys.exit("Chrome не найден, передайте путь первым аргументом")


def main():
    page = (ROOT / "Tools" / "assets.html").as_uri()
    dom = subprocess.run([find_chrome(), "--headless=new", "--disable-gpu", "--dump-dom", page],
                         check=True, capture_output=True, text=True).stdout
    body = re.search(r'<pre id="out">(.*?)</pre>', dom, re.S)
    if not body or not body.group(1).strip():
        sys.exit("генератор ничего не вернул")

    out = ROOT / "Assets"
    out.mkdir(exist_ok=True)
    for line in html.unescape(body.group(1)).strip().splitlines():
        name, data = line.split(" ", 1)
        (out / f"{name}.png").write_bytes(base64.b64decode(data))
        print(f"Assets/{name}.png")


if __name__ == "__main__":
    main()
