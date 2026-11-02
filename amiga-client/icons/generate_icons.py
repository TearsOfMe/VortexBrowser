#!/usr/bin/env python3
"""Generate crisp 20x20 RGBA webpage icons for VortexBrowse AmigaOS 4 client."""

import os
from PIL import Image, ImageDraw, ImageFont

ICONS_DIR = os.path.dirname(os.path.abspath(__file__))

def create_base(size=(20, 20)):
    return Image.new("RGBA", size, (0, 0, 0, 0))

def draw_youtube():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Red rounded rect
    draw.rounded_rectangle([1, 3, 18, 16], radius=4, fill=(255, 0, 0, 255))
    # White play triangle
    draw.polygon([(8, 6), (14, 10), (8, 14)], fill=(255, 255, 255, 255))
    return im

def draw_amiga_news():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Red & White boing ball style or checkmark
    draw.ellipse([1, 1, 18, 18], fill=(220, 20, 20, 255), outline=(150, 0, 0, 255))
    # Checkmark / A letter
    draw.line([(5, 11), (9, 15), (15, 5)], fill=(255, 255, 255, 255), width=2)
    return im

def draw_os4depot():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Blue AmigaOS cube / drawer
    draw.rectangle([2, 5, 17, 16], fill=(0, 102, 204, 255), outline=(0, 60, 140, 255))
    draw.polygon([(2, 5), (6, 2), (13, 2), (17, 5)], fill=(51, 153, 255, 255))
    # '4' in white
    draw.line([(8, 7), (8, 12), (12, 12)], fill=(255, 255, 255, 255), width=2)
    draw.line([(11, 7), (11, 14)], fill=(255, 255, 255, 255), width=2)
    return im

def draw_amigans():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Cyan/Blue circle with 'A'
    draw.ellipse([1, 1, 18, 18], fill=(30, 144, 255, 255), outline=(0, 100, 200, 255))
    # White 'A'
    draw.line([(10, 4), (5, 15)], fill=(255, 255, 255, 255), width=2)
    draw.line([(10, 4), (15, 15)], fill=(255, 255, 255, 255), width=2)
    draw.line([(7, 11), (13, 11)], fill=(255, 255, 255, 255), width=2)
    return im

def draw_aminet():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Green sphere / tree
    draw.ellipse([2, 2, 17, 17], fill=(46, 139, 87, 255), outline=(34, 100, 60, 255))
    # Network nodes
    draw.ellipse([4, 4, 8, 8], fill=(255, 255, 255, 255))
    draw.ellipse([11, 4, 15, 8], fill=(255, 255, 255, 255))
    draw.ellipse([7, 11, 12, 15], fill=(255, 255, 255, 255))
    draw.line([(6, 6), (9, 13), (13, 6)], fill=(255, 255, 255, 255), width=1)
    return im

def draw_twitch():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Purple speech bubble
    draw.rounded_rectangle([2, 2, 17, 15], radius=3, fill=(145, 70, 255, 255))
    draw.polygon([(5, 15), (5, 18), (8, 15)], fill=(145, 70, 255, 255))
    # Eyes
    draw.rectangle([6, 6, 8, 11], fill=(255, 255, 255, 255))
    draw.rectangle([11, 6, 13, 11], fill=(255, 255, 255, 255))
    return im

def draw_wikipedia():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Light gray circle
    draw.ellipse([1, 1, 18, 18], fill=(240, 240, 240, 255), outline=(180, 180, 180, 255))
    # Black 'W'
    draw.line([(4, 5), (6, 14), (9, 8), (12, 14), (15, 5)], fill=(20, 20, 20, 255), width=2)
    return im

def draw_google():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # White background circle
    draw.ellipse([1, 1, 18, 18], fill=(255, 255, 255, 255), outline=(210, 210, 210, 255))
    # Red/Yellow/Green/Blue segments
    draw.arc([3, 3, 16, 16], start=45, end=180, fill=(234, 67, 53, 255), width=3)
    draw.arc([3, 3, 16, 16], start=180, end=270, fill=(251, 188, 5, 255), width=3)
    draw.arc([3, 3, 16, 16], start=270, end=340, fill=(52, 168, 83, 255), width=3)
    draw.line([(10, 10), (16, 10)], fill=(66, 133, 244, 255), width=3)
    return im

def draw_github():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Dark circle
    draw.ellipse([1, 1, 18, 18], fill=(36, 41, 46, 255))
    # White cat outline
    draw.ellipse([4, 6, 15, 15], fill=(255, 255, 255, 255))
    draw.polygon([(5, 7), (7, 3), (9, 7)], fill=(255, 255, 255, 255))
    draw.polygon([(14, 7), (12, 3), (10, 7)], fill=(255, 255, 255, 255))
    return im

def draw_news():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Folded newspaper
    draw.rectangle([2, 4, 17, 16], fill=(245, 245, 245, 255), outline=(100, 100, 100, 255))
    draw.rectangle([4, 6, 8, 10], fill=(70, 130, 180, 255))
    draw.line([(10, 6), (15, 6)], fill=(80, 80, 80, 255), width=1)
    draw.line([(10, 8), (15, 8)], fill=(80, 80, 80, 255), width=1)
    draw.line([(4, 12), (15, 12)], fill=(80, 80, 80, 255), width=1)
    draw.line([(4, 14), (13, 14)], fill=(80, 80, 80, 255), width=1)
    return im

def draw_globe():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Blue globe with green continents
    draw.ellipse([1, 1, 18, 18], fill=(30, 144, 255, 255), outline=(0, 80, 180, 255))
    draw.ellipse([4, 4, 10, 11], fill=(46, 139, 87, 255))
    draw.ellipse([10, 8, 16, 14], fill=(46, 139, 87, 255))
    return im

def draw_drawer():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Yellow/Amber folder
    draw.polygon([(2, 5), (6, 5), (8, 7), (17, 7), (17, 16), (2, 16)], fill=(255, 200, 50, 255), outline=(200, 140, 20, 255))
    return im

def draw_bookmark():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Blue ribbon
    draw.polygon([(4, 2), (15, 2), (15, 17), (9, 13), (4, 17)], fill=(0, 120, 215, 255), outline=(0, 70, 160, 255))
    return im

def draw_video():
    return draw_youtube()

def draw_favorite():
    im = create_base()
    draw = ImageDraw.Draw(im)
    # Gold star
    draw.polygon([(10, 2), (12, 7), (17, 8), (13, 12), (14, 17), (10, 14), (5, 17), (6, 12), (2, 8), (7, 7)],
                 fill=(255, 215, 0, 255), outline=(218, 165, 32, 255))
    return im

def main():
    import urllib.request
    import io

    real_favicons = {
        "amiga-news": "https://www.amiga-news.de/favicon.ico",
        "os4depot":   "https://os4depot.net/favicon.ico",
        "amigans":    "https://www.amigans.net/favicon.ico",
        "aminet":     "https://aminet.net/favicon.ico",
        "youtube":    "https://www.youtube.com/favicon.ico",
        "wikipedia":  "https://de.wikipedia.org/static/apple-touch/wikipedia.png",
        "twitch":     "https://m.twitch.tv/favicon.ico",
        "google":     "https://www.google.com/favicon.ico",
        "github":     "https://github.com/favicon.ico",
    }
    headers = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}

    generators = {
        "amiga-news": draw_amiga_news,
        "os4depot":   draw_os4depot,
        "amigans":    draw_amigans,
        "aminet":     draw_aminet,
        "youtube":    draw_youtube,
        "twitch":     draw_twitch,
        "wikipedia":  draw_wikipedia,
        "google":     draw_google,
        "github":     draw_github,
        "news":       draw_news,
        "video":      draw_video,
        "globe":      draw_globe,
        "drawer":     draw_drawer,
        "bookmark":   draw_bookmark,
        "favorite":   draw_favorite,
    }

    for name, gen in generators.items():
        out_path = os.path.join(ICONS_DIR, f"{name}.png")
        downloaded = False
        if name in real_favicons:
            try:
                req = urllib.request.Request(real_favicons[name], headers=headers)
                data = urllib.request.urlopen(req, timeout=4).read()
                im = Image.open(io.BytesIO(data)).convert("RGBA")
                im = im.resize((20, 20), Image.Resampling.LANCZOS)
                im.save(out_path, format="PNG")
                downloaded = True
                print(f"Downloaded real favicon: {out_path} (20x20 RGBA)")
            except Exception as e:
                print(f"Could not download {name} ({e}), falling back to procedural generator")

        if not downloaded:
            img = gen()
            img.save(out_path, format="PNG")
            print(f"Generated procedural: {out_path} (20x20 RGBA)")

if __name__ == "__main__":
    main()
