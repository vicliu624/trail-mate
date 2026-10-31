"""Draw original 64px radio icons matching the existing transparent pixel set."""
from pathlib import Path
import runpy
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
INK = '#57391f'
SHADE = '#b6673d'
BODY = '#e99a64'
LIGHT = '#ffd38a'
GLASS = '#81babc'
GLINT = '#c5e0d5'


def radio():
    image = Image.new('RGBA', (64, 64))
    d = ImageDraw.Draw(image)
    d.line([(12, 24), (8, 4)], fill=INK, width=3)
    d.line([(12, 23), (9, 5)], fill=GLASS, width=1)
    d.rounded_rectangle((20, 14, 47, 24), radius=3, outline=INK, width=2)
    d.rounded_rectangle((7, 22, 57, 56), radius=5, fill=SHADE, outline=INK, width=2)
    d.rounded_rectangle((8, 23, 55, 53), radius=4, fill=BODY, outline=LIGHT, width=1)
    d.rounded_rectangle((12, 27, 52, 35), radius=1, fill=INK)
    d.rectangle((14, 28, 50, 33), fill=GLASS)
    for x in range(18, 49, 5):
        d.line((x, 29, x, 31), fill=INK)
    d.line((35, 27, 35, 34), fill=LIGHT)
    d.ellipse((12, 38, 33, 50), fill=INK)
    for y in (40, 43, 46, 49):
        d.line((16, y, 29, y), fill=SHADE)
    d.ellipse((39, 38, 50, 49), fill=INK)
    d.ellipse((41, 40, 48, 47), fill=LIGHT)
    d.line((44, 40, 44, 43), fill=INK)
    d.rectangle((14, 56, 21, 58), fill=INK)
    d.rectangle((44, 56, 51, 58), fill=INK)
    return image


def television():
    image = Image.new('RGBA', (64, 64))
    d = ImageDraw.Draw(image)
    d.line([(18, 5), (31, 19), (44, 5)], fill=INK, width=2)
    d.line([(20, 6), (31, 17), (42, 6)], fill=GLASS, width=1)
    d.ellipse((27, 16, 36, 22), fill=SHADE, outline=INK)
    d.rounded_rectangle((5, 21, 58, 54), radius=5, fill=SHADE, outline=INK, width=2)
    d.rounded_rectangle((6, 22, 56, 51), radius=4, fill=BODY, outline=LIGHT)
    d.rounded_rectangle((10, 25, 44, 48), radius=4, fill=INK)
    d.rounded_rectangle((12, 27, 42, 46), radius=3, fill=GLASS)
    d.line((15, 29, 37, 29), fill=GLINT)
    d.line((14, 30, 14, 40), fill=GLINT)
    d.line((18, 43, 38, 43), fill='#659599')
    d.ellipse((47, 27, 53, 33), fill=INK)
    d.rectangle((49, 28, 50, 30), fill=LIGHT)
    d.ellipse((47, 36, 53, 42), fill=INK)
    d.rectangle((49, 37, 50, 39), fill=LIGHT)
    d.line((48, 46, 53, 46), fill=INK)
    d.rectangle((13, 54, 20, 57), fill=INK)
    d.rectangle((43, 54, 50, 57), fill=INK)
    return image


if __name__ == '__main__':
    convert = runpy.run_path(str(ROOT / 'scripts/png_to_lvgl.py'))['convert_png_to_lvgl']
    for name, image in (('fm_radio_icon', radio()), ('oneseg_tv_icon', television())):
        png = ROOT / 'images' / (name + '.png')
        image.save(png)
        convert(str(png), str(ROOT / 'modules/ui_shared/src/ui/assets' / (name + '.c')), name)
