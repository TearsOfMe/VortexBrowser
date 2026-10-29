#!/usr/bin/env python3
"""
Generate native AmigaOS 4 / OS3.5 icons for VortexBrowser.
Creates VortexBrowser.info with:
- VideoVortex cosmic black hole / accretion disk aesthetic
- "Vortex" (line 1) and "Browser" (line 2) in glowing futuristic typography
- Normal and selected states
- Classic 8-bitplane planar fallback
- ColorIcon (GlowIcon) 251-color indexed with transparency
- OS4 ARGB 32-bit zlib-compressed truecolor graphics
"""

import os
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageEnhance

# Use our amigainfo package
sys.path.insert(0, "/tmp/amigainfo/src")
import amigainfo
from amigainfo.models import (
    DiskObject, Gadget, ClassicImage, ClassicImages, ImageHeader,
    ColorIconImage, ColorIconImages, FaceChunk, ARGBImage, ARGBImages,
    IconType
)

def render_vortex_icon_states():
    clean_base_path = "/home/tears/.gemini/antigravity/brain/c2984b2a-1531-47e4-9dc5-b90aca1b2d93/vv_base_clean.png"
    clean = Image.open(clean_base_path).convert("RGBA")
    font_path = "/usr/share/fonts/TTF/FiraSans-ExtraBoldItalic.ttf"

    scale = 4
    canvas_w = clean.width * scale
    canvas_h = clean.height * scale

    # --- 1. NORMAL STATE ---
    text_layer = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    t_draw = ImageDraw.Draw(text_layer)

    f_size1 = int(13.0 * scale)
    f_size2 = int(11.0 * scale)
    font1 = ImageFont.truetype(font_path, f_size1)
    font2 = ImageFont.truetype(font_path, f_size2)

    text1 = "Vortex"
    text2 = "Browser"

    bbox1 = t_draw.textbbox((0, 0), text1, font=font1)
    bbox2 = t_draw.textbbox((0, 0), text2, font=font2)
    w1, h1 = bbox1[2] - bbox1[0], bbox1[3] - bbox1[1]
    w2, h2 = bbox2[2] - bbox2[0], bbox2[3] - bbox2[1]

    center_x = 64 * scale
    center_y = 53 * scale

    x1 = center_x - w1 // 2 - bbox1[0] - int(1.0 * scale)
    y1 = center_y - (h1 + h2 + int(2.0 * scale)) // 2 - bbox1[1] - int(0.5 * scale)

    x2 = center_x - w2 // 2 - bbox2[0] - int(1.0 * scale)
    y2 = y1 + h1 + int(2.5 * scale)

    # Fiery orange/red outer glow
    glow_layer = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    g_draw = ImageDraw.Draw(glow_layer)
    stroke_w = int(2.0 * scale)
    g_draw.text((x1, y1), text1, font=font1, fill=(255, 50, 0, 255), stroke_width=stroke_w, stroke_fill=(220, 20, 0, 255))
    g_draw.text((x2, y2), text2, font=font2, fill=(255, 50, 0, 255), stroke_width=stroke_w, stroke_fill=(220, 20, 0, 255))
    glow_blurred = glow_layer.filter(ImageFilter.GaussianBlur(radius=scale * 0.7))

    # Main text with bright golden core and fiery outline
    stroke_inner = int(1.1 * scale)
    t_draw.text((x1, y1), text1, font=font1, fill=(255, 235, 70, 255), stroke_width=stroke_inner, stroke_fill=(255, 90, 0, 255))
    t_draw.text((x2, y2), text2, font=font2, fill=(255, 235, 70, 255), stroke_width=stroke_inner, stroke_fill=(255, 90, 0, 255))

    base_up = clean.resize((canvas_w, canvas_h), Image.Resampling.LANCZOS)
    comp_norm = Image.alpha_composite(base_up, glow_blurred)
    comp_norm = Image.alpha_composite(comp_norm, text_layer)
    im_norm = comp_norm.resize((clean.width, clean.height), Image.Resampling.LANCZOS)

    # --- 2. SELECTED STATE (Bright electric glow & highlight) ---
    enhancer = ImageEnhance.Brightness(clean)
    clean_sel = enhancer.enhance(1.25)
    enhancer2 = ImageEnhance.Contrast(clean_sel)
    clean_sel = enhancer2.enhance(1.15)

    text_layer_sel = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    t_draw_sel = ImageDraw.Draw(text_layer_sel)

    glow_layer_sel = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 0))
    g_draw_sel = ImageDraw.Draw(glow_layer_sel)
    stroke_w_sel = int(2.5 * scale)
    g_draw_sel.text((x1, y1), text1, font=font1, fill=(255, 180, 0, 255), stroke_width=stroke_w_sel, stroke_fill=(255, 100, 0, 255))
    g_draw_sel.text((x2, y2), text2, font=font2, fill=(255, 180, 0, 255), stroke_width=stroke_w_sel, stroke_fill=(255, 100, 0, 255))
    glow_blurred_sel = glow_layer_sel.filter(ImageFilter.GaussianBlur(radius=scale * 1.0))

    t_draw_sel.text((x1, y1), text1, font=font1, fill=(255, 255, 220, 255), stroke_width=stroke_inner, stroke_fill=(255, 140, 0, 255))
    t_draw_sel.text((x2, y2), text2, font=font2, fill=(255, 255, 220, 255), stroke_width=stroke_inner, stroke_fill=(255, 140, 0, 255))

    base_up_sel = clean_sel.resize((canvas_w, canvas_h), Image.Resampling.LANCZOS)
    comp_sel = Image.alpha_composite(base_up_sel, glow_blurred_sel)
    comp_sel = Image.alpha_composite(comp_sel, text_layer_sel)
    im_sel = comp_sel.resize((clean.width, clean.height), Image.Resampling.LANCZOS)

    return im_norm, im_sel

def make_info_file(im_norm, im_sel, icon_type=IconType.TOOL, output_path="VortexBrowser.info"):
    w, h = im_norm.size

    # --- ARGB CHUNKS ---
    def make_argb(img):
        argb_bytes = bytearray()
        for y in range(h):
            for x in range(w):
                r, g, b, a = img.getpixel((x, y))
                argb_bytes.extend([a, r, g, b])
        return ARGBImage(width=w, height=h, data=bytes(argb_bytes))

    argb = ARGBImages(normal=make_argb(im_norm), selected=make_argb(im_sel))

    # --- COLORICON CHUNKS (Indexed 251 colors + transparent) ---
    def make_ci(img):
        r, g, b, a = img.split()
        rgb = Image.merge("RGB", (r, g, b))
        rgb_q = rgb.quantize(colors=250, method=Image.Quantize.MEDIANCUT)
        pal_raw = rgb_q.getpalette()[:250*3]
        pal = [(0, 0, 0)] + [(pal_raw[i*3], pal_raw[i*3+1], pal_raw[i*3+2]) for i in range(250)]

        pixels = bytearray(w * h)
        a_bytes = a.tobytes()
        q_bytes = rgb_q.tobytes()
        for i in range(w * h):
            pixels[i] = 0 if a_bytes[i] < 64 else (q_bytes[i] + 1)

        ci = ColorIconImage(
            width=w, height=h, transparent_color=0, has_transparent=True,
            depth=8, palette=pal, pixel_data=bytes(pixels)
        )
        return ci, pixels

    ci_norm, pixels_norm = make_ci(im_norm)
    ci_sel, pixels_sel = make_ci(im_sel)

    face = FaceChunk(width=w, height=h, flags=0, aspect_x=0, aspect_y=0, max_palette_bytes=256)
    coloricon = ColorIconImages(face=face, normal=ci_norm, selected=ci_sel)

    # --- CLASSIC PLANAR CHUNKS ---
    bytes_per_row = ((w + 15) // 16) * 2
    def make_classic_planes(px_data):
        planes = [bytearray(bytes_per_row * h) for _ in range(8)]
        for y in range(h):
            for x in range(w):
                val = px_data[y * w + x]
                byte_idx = y * bytes_per_row + (x // 8)
                bit_idx = 7 - (x % 8)
                for p in range(8):
                    if (val >> p) & 1:
                        planes[p][byte_idx] |= (1 << bit_idx)
        header = ImageHeader(left_edge=0, top_edge=0, width=w, height=h, depth=8, plane_pick=0xFF, plane_on_off=0)
        return ClassicImage(header=header, planes=[bytes(p) for p in planes])

    classic = ClassicImages(
        normal=make_classic_planes(pixels_norm),
        selected=make_classic_planes(pixels_sel)
    )

    gadget = Gadget(
        left_edge=0, top_edge=0, width=w, height=h,
        flags=0x0006, # GADGIMAGE | GADGHIMAGE
        activation=0x0001, # RELVERIFY
        gadget_type=0x0001, # BOOLGADGET
        gadget_render=1, select_render=1, user_data=1
    )

    NO_ICON_POSITION = -2147483648

    dobj = DiskObject(
        magic=0xE310, version=1, gadget=gadget, type=icon_type,
        current_x=NO_ICON_POSITION, current_y=NO_ICON_POSITION,
        stack_size=200000,
        tooltypes=["(SERVER=127.0.0.1)", "(PORT=7788)"] if icon_type == IconType.TOOL else [],
        classic=classic, coloricon=coloricon, argb=argb
    )

    out_bytes = amigainfo.save(dobj)
    with open(output_path, "wb") as f:
        f.write(out_bytes)
    print(f"[+] Successfully wrote {output_path} ({len(out_bytes)} bytes)")

if __name__ == "__main__":
    im_norm, im_sel = render_vortex_icon_states()
    im_norm.save("/home/tears/.gemini/antigravity/brain/c2984b2a-1531-47e4-9dc5-b90aca1b2d93/VortexBrowser_norm.png")
    im_sel.save("/home/tears/.gemini/antigravity/brain/c2984b2a-1531-47e4-9dc5-b90aca1b2d93/VortexBrowser_sel.png")

    script_dir = os.path.dirname(os.path.abspath(__file__))
    tool_info_path = os.path.join(script_dir, "VortexBrowser.info")
    make_info_file(im_norm, im_sel, IconType.TOOL, tool_info_path)

    # Also make VortexBrowse.info for backward compatibility
    compat_info_path = os.path.join(script_dir, "VortexBrowse.info")
    make_info_file(im_norm, im_sel, IconType.TOOL, compat_info_path)

    # Also make drawer icon for the VortexBrowser directory
    drawer_info_path = os.path.join(script_dir, "VortexBrowser_drawer.info")
    make_info_file(im_norm, im_sel, IconType.DRAWER, drawer_info_path)
