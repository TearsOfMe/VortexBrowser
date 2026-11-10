#!/usr/bin/env python3
"""
Generate native AmigaOS 4.1 (64x64 standard size) circular .info icons for VortexBrowser.
Creates:
  1. Primary icon set in amiga-client/ (Cyber-Globe Vortex, circular 32-bit ARGB):
     - VortexBrowser.info          (Standard ReAction)
     - VortexBrowse.info           (Backward-compatibility alias)
     - VortexBrowser-AltiVec.info  (with 'AltiVec' badge)
     - VortexBrowser-MUI.info      (with 'MUI' badge)
     - VortexBrowser_drawer.info   (Workbench drawer icon with DrawerData)
  2. Alternative icon set in amiga-client/alt-icons/ (3D Titanium Glossy Orb):
     - VortexBrowser.info
     - VortexBrowser-AltiVec.info  (with 'AltiVec' badge)
     - VortexBrowser-MUI.info      (with 'MUI' badge)
     - VortexBrowser_drawer.info   (Workbench drawer icon with DrawerData)

Self-contained pure Python + Pillow writer (no external amigainfo dependency):
  - DiskObject + OS2/OS4 DrawerData (for WBDRAWER) + ToolTypes (for WBTOOL)
  - Classic 2-bitplane planar fallback (matching AmigaOS 4.1 FE standard icons)
  - IFF FORM ICON: FACE (0x02 truecolor mode) + ARGB (normal & selected 32-bit zlib OS4)
"""

import os
import struct
import zlib
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageEnhance

WBDRAWER = 2
WBTOOL = 3
NO_ICON_POSITION = -2147483648
ICON_SIZE = 64

FONT_TITLE_1 = "/usr/share/fonts/TTF/FiraSans-ExtraBoldItalic.ttf"
FONT_TITLE_2 = "/usr/share/fonts/TTF/FiraSansCondensed-ExtraBoldItalic.ttf"
FONT_BADGE = "/usr/share/fonts/TTF/FiraSansCondensed-ExtraBold.ttf"


def _add_vortex_typography(canvas_rgba, selected=False, has_badge=False):
    S = canvas_rgba.width  # 512 supersampled
    text_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    glow_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    td = ImageDraw.Draw(text_layer)
    gd = ImageDraw.Draw(glow_layer)

    scale_f = 0.86 if not has_badge else 0.82
    f1 = ImageFont.truetype(FONT_TITLE_1, int(S * 0.152 * scale_f))
    f2 = ImageFont.truetype(FONT_TITLE_2, int(S * 0.136 * scale_f))

    t1, t2 = "Vortex", "Browser"
    b1 = td.textbbox((0, 0), t1, font=f1)
    b2 = td.textbbox((0, 0), t2, font=f2)
    w1, h1 = b1[2] - b1[0], b1[3] - b1[1]
    w2, h2 = b2[2] - b2[0], b2[3] - b2[1]

    gap = int(S * 0.012)
    total_h = h1 + h2 + gap
    cx = S // 2
    cy = S // 2 - (int(S * 0.048) if has_badge else 0)

    x1 = cx - w1 // 2 - b1[0] - int(S * 0.005)
    y1 = cy - total_h // 2 - b1[1]
    x2 = cx - w2 // 2 - b2[0] - int(S * 0.005)
    y2 = y1 + b1[3] + gap - b2[1]

    shadow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sw_dark = max(2, int(S * 0.026))
    sd.text((x1, y1 + int(S * 0.006)), t1, font=f1, fill=(0, 0, 0, 240), stroke_width=sw_dark, stroke_fill=(0, 0, 0, 240))
    sd.text((x2, y2 + int(S * 0.006)), t2, font=f2, fill=(0, 0, 0, 240), stroke_width=sw_dark, stroke_fill=(0, 0, 0, 240))
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=S * 0.013))

    if not selected:
        sw_glow = max(2, int(S * 0.020))
        gd.text((x1, y1), t1, font=f1, fill=(255, 55, 0, 255), stroke_width=sw_glow, stroke_fill=(225, 20, 0, 255))
        gd.text((x2, y2), t2, font=f2, fill=(255, 55, 0, 255), stroke_width=sw_glow, stroke_fill=(225, 20, 0, 255))
        glow = glow_layer.filter(ImageFilter.GaussianBlur(radius=S * 0.010))

        sw_in = max(1, int(S * 0.011))
        td.text((x1, y1), t1, font=f1, fill=(255, 242, 90, 255), stroke_width=sw_in, stroke_fill=(255, 90, 0, 255))
        td.text((x2, y2), t2, font=f2, fill=(255, 242, 90, 255), stroke_width=sw_in, stroke_fill=(255, 90, 0, 255))
    else:
        sw_glow = max(2, int(S * 0.026))
        gd.text((x1, y1), t1, font=f1, fill=(255, 195, 0, 255), stroke_width=sw_glow, stroke_fill=(255, 115, 0, 255))
        gd.text((x2, y2), t2, font=f2, fill=(255, 195, 0, 255), stroke_width=sw_glow, stroke_fill=(255, 115, 0, 255))
        glow = glow_layer.filter(ImageFilter.GaussianBlur(radius=S * 0.015))

        sw_in = max(1, int(S * 0.011))
        td.text((x1, y1), t1, font=f1, fill=(255, 255, 230, 255), stroke_width=sw_in, stroke_fill=(255, 145, 0, 255))
        td.text((x2, y2), t2, font=f2, fill=(255, 255, 230, 255), stroke_width=sw_in, stroke_fill=(255, 145, 0, 255))

    out = Image.alpha_composite(canvas_rgba, shadow)
    out = Image.alpha_composite(out, glow)
    out = Image.alpha_composite(out, text_layer)
    return out


def _add_bottom_badge(canvas_rgba, badge_text, selected=False):
    if not badge_text:
        return canvas_rgba

    S = canvas_rgba.width  # 512
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    is_altivec = badge_text.lower() == "altivec"
    font_px = int(S * (0.132 if is_altivec else 0.142))
    f_badge = ImageFont.truetype(FONT_BADGE, font_px)
    tb = d.textbbox((0, 0), badge_text, font=f_badge)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]

    pad_x = int(S * (0.055 if is_altivec else 0.070))
    pill_w = tw + 2 * pad_x
    pill_h = int(S * 0.205)
    x0 = (S - pill_w) // 2
    x1 = x0 + pill_w
    y1 = S - int(S * 0.020)
    y0 = y1 - pill_h
    rad = pill_h // 2

    # Badge drop shadow
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle([x0 - 3, y0 + 4, x1 + 3, y1 + 6], radius=rad, fill=(0, 0, 0, 210))
    sh = sh.filter(ImageFilter.GaussianBlur(radius=S * 0.014))
    out = Image.alpha_composite(canvas_rgba, sh)

    if is_altivec:
        bg_col = (0, 88, 205, 250) if not selected else (0, 122, 245, 255)
        border_col = (90, 242, 255, 255) if not selected else (200, 255, 255, 255)
        txt_col = (255, 255, 255, 255)
        stroke_col = (0, 38, 105, 255)
    else:  # MUI
        bg_col = (108, 24, 178, 250) if not selected else (142, 42, 222, 255)
        border_col = (255, 215, 90, 255) if not selected else (255, 245, 175, 255)
        txt_col = (255, 244, 175, 255) if not selected else (255, 255, 235, 255)
        stroke_col = (52, 8, 95, 255)

    bw = max(3, int(S * 0.013))
    d.rounded_rectangle([x0, y0, x1, y1], radius=rad, fill=bg_col, outline=border_col, width=bw)

    tx = (S - tw) // 2 - tb[0]
    ty = y0 + (pill_h - th) // 2 - tb[1] - int(S * 0.004)
    d.text((tx, ty), badge_text, font=f_badge, fill=txt_col, stroke_width=max(2, int(S * 0.010)), stroke_fill=stroke_col)

    return Image.alpha_composite(out, layer)


def _downsample_sharp(img_hi, target_size=ICON_SIZE):
    im = img_hi.resize((target_size, target_size), Image.Resampling.LANCZOS)
    r, g, b, a = im.split()
    rgb = Image.merge("RGB", (r, g, b)).filter(ImageFilter.UnsharpMask(radius=0.65, percent=75, threshold=2))
    r2, g2, b2 = rgb.split()
    return Image.merge("RGBA", (r2, g2, b2, a))


def render_circular_icon_pair(base_png_path, badge=None, target_size=ICON_SIZE):
    S = 512
    orb_src = Image.open(base_png_path).convert("RGBA")
    margin = int(S * 0.045)
    diam = S - 2 * margin
    orb = orb_src.resize((diam, diam), Image.Resampling.LANCZOS)

    # --- 1. NORMAL STATE ---
    norm_canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    drop = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(drop).ellipse((margin + 2, margin + 6, margin + diam - 2, margin + diam + 5), fill=(0, 0, 0, 175))
    drop = drop.filter(ImageFilter.GaussianBlur(radius=S * 0.020))
    norm_canvas = Image.alpha_composite(norm_canvas, drop)

    orb_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    orb_layer.paste(orb, (margin, margin), orb)
    norm_comp = Image.alpha_composite(norm_canvas, orb_layer)
    norm_comp = _add_vortex_typography(norm_comp, selected=False, has_badge=bool(badge))
    norm_comp = _add_bottom_badge(norm_comp, badge, selected=False)

    # --- 2. SELECTED STATE ---
    sel_canvas = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    aura = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ad = ImageDraw.Draw(aura)
    ad.pieslice((margin - 8, margin - 8, margin + diam + 8, margin + diam + 8), 90, 270, fill=(0, 220, 255, 215))
    ad.pieslice((margin - 8, margin - 8, margin + diam + 8, margin + diam + 8), -90, 90, fill=(255, 150, 0, 215))
    aura = aura.filter(ImageFilter.GaussianBlur(radius=S * 0.028))
    sel_canvas = Image.alpha_composite(sel_canvas, aura)

    r_ch, g_ch, b_ch, a_ch = orb.split()
    rgb_bright = ImageEnhance.Brightness(Image.merge("RGB", (r_ch, g_ch, b_ch))).enhance(1.18)
    rgb_bright = ImageEnhance.Contrast(rgb_bright).enhance(1.10)
    rb, gb, bb = rgb_bright.split()
    orb_sel = Image.merge("RGBA", (rb, gb, bb, a_ch))

    rim = Image.new("RGBA", (diam, diam), (0, 0, 0, 0))
    ImageDraw.Draw(rim).ellipse((2, 2, diam - 3, diam - 3), outline=(255, 238, 160, 170), width=max(3, int(diam * 0.012)))
    orb_sel = Image.alpha_composite(orb_sel, rim)

    sel_orb_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sel_orb_layer.paste(orb_sel, (margin, margin), orb_sel)
    sel_comp = Image.alpha_composite(sel_canvas, sel_orb_layer)
    sel_comp = _add_vortex_typography(sel_comp, selected=True, has_badge=bool(badge))
    sel_comp = _add_bottom_badge(sel_comp, badge, selected=True)

    return _downsample_sharp(norm_comp, target_size), _downsample_sharp(sel_comp, target_size)


def _iff_chunk(cid, payload):
    pad = b"\x00" if (len(payload) & 1) else b""
    return cid + struct.pack(">I", len(payload)) + payload + pad


def _encode_classic_2bit(img):
    """Encode standard 2-bitplane planar fallback matching official AmigaOS 4.1 FE icons."""
    w, h = img.size
    rgba = img.tobytes()
    bpr = ((w + 15) // 16) * 2
    plane0 = bytearray(bpr * h)
    plane1 = bytearray(bpr * h)

    for y in range(h):
        row_px = y * w * 4
        p_row = y * bpr
        for x in range(w):
            off = row_px + x * 4
            r, g, b, a = rgba[off], rgba[off + 1], rgba[off + 2], rgba[off + 3]
            if a < 96:
                idx = 0  # Transparent background
            else:
                luma = (r * 77 + g * 150 + b * 29) >> 8
                if luma < 85:
                    idx = 1  # Dark / black pen
                elif luma > 185:
                    idx = 2  # Highlight / white pen
                else:
                    idx = 3  # Midtone / blue-accent pen
            if idx:
                byte_idx = p_row + (x >> 3)
                mask = 1 << (7 - (x & 7))
                if idx & 1:
                    plane0[byte_idx] |= mask
                if idx & 2:
                    plane1[byte_idx] |= mask

    # struct Image header (20 bytes): depth=2, ImageData=1 (non-null), PlanePick=0x03, PlaneOnOff=0x00
    img_hdr = struct.pack(">hhhhHIBBI", 0, 0, w, h, 2, 1, 0x03, 0x00, 0)
    return img_hdr + bytes(plane0) + bytes(plane1)


def _encode_os4_argb(img):
    """Encode AmigaOS 4.1 32-bit ARGB zlib chunk payload."""
    rgba = img.tobytes()
    argb = bytearray(len(rgba))
    argb[0::4] = rgba[3::4]  # A
    argb[1::4] = rgba[0::4]  # R
    argb[2::4] = rgba[1::4]  # G
    argb[3::4] = rgba[2::4]  # B
    zdata = zlib.compress(bytes(argb))  # Standard zlib header 0x789c matching OS4 z.library
    hdr = struct.pack(">IIH", 1, len(zdata) - 1, 0)
    return hdr + zdata


def write_amiga_info(im_norm, im_sel, icon_type, output_path, tooltypes=None, stack_size=200000):
    w, h = im_norm.size
    classic_norm = _encode_classic_2bit(im_norm)
    classic_sel = _encode_classic_2bit(im_sel)

    has_drawer = (icon_type == WBDRAWER)
    has_tt = bool(tooltypes)

    # struct Gadget (44 bytes)
    gadget = struct.pack(
        ">IhhhhHHHIIIIIHI",
        0,          # NextGadget
        0, 0,       # LeftEdge, TopEdge
        w, h,       # Width, Height
        0x0006,     # Flags: GADGIMAGE | GADGHIMAGE
        0x0001,     # Activation: RELVERIFY
        0x0001,     # GadgetType: BOOLGADGET
        1, 1,       # GadgetRender, SelectRender (non-zero presence pointers)
        0, 0, 0,    # GadgetText, MutualExclude, SpecialInfo
        0,          # GadgetID
        1,          # UserData = WB_DISKREVISION (1)
    )

    # struct DiskObject (4 + 44 + 30 = 78 bytes)
    dobj = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIiiIIi",
        icon_type,
        0,
        0,                              # do_DefaultTool
        1 if has_tt else 0,             # do_ToolTypes
        NO_ICON_POSITION,
        NO_ICON_POSITION,
        1 if has_drawer else 0,         # do_DrawerData
        0,                              # do_ToolWindow
        stack_size if icon_type == WBTOOL else 65536,
    )

    out = bytearray(dobj)

    if has_drawer:
        # 56-byte OldDrawerData (NewWindow 48B + dd_CurrentX/Y 8B) matching native OS4 drawers
        old_dd = bytes.fromhex(
            "004d003201be00feffff000000000200107f00000000000000000000000000000000000000000068005fffffffff0001"
            "0000000000000000"
        )
        out.extend(old_dd)

    out.extend(classic_norm)
    out.extend(classic_sel)

    if has_tt:
        out.extend(struct.pack(">I", (len(tooltypes) + 1) * 4))
        for tt in tooltypes:
            b_tt = tt.encode("latin1") + b"\x00"
            out.extend(struct.pack(">I", len(b_tt)) + b_tt)

    if has_drawer:
        # 6-byte DrawerData2 (dd_Flags=2 show all files, dd_ViewModes=1 icon view)
        out.extend(b"\x00\x00\x00\x02\x00\x01")

    # Genuine AmigaOS 4.1 IFF FORM ICON:
    # Note: In OS4 icon.library, FACE.flags bit 1 (0x02) selects 32-bit ARGB mode and MUST NOT
    # be mixed with 8-bit IMAG chunks (otherwise icon.library aborts with ERROR_OBJECT_WRONG_TYPE=212).
    face_chunk = _iff_chunk(b"FACE", bytes([w - 1, h - 1, 0x02, 0x00, 0x00, 0x01]))
    argb1_chunk = _iff_chunk(b"ARGB", _encode_os4_argb(im_norm))
    argb2_chunk = _iff_chunk(b"ARGB", _encode_os4_argb(im_sel))

    icon_body = b"ICON" + face_chunk + argb1_chunk + argb2_chunk
    form_iff = b"FORM" + struct.pack(">I", len(icon_body)) + icon_body
    out.extend(form_iff)

    os.makedirs(os.path.dirname(os.path.abspath(output_path)), exist_ok=True)
    with open(output_path, "wb") as f:
        f.write(out)
    print(f"[+] Wrote {output_path} ({w}x{h}, type={icon_type}, {len(out)} bytes)")


def build_icon_set(base_png, out_dir, include_compat=False):
    default_tt = ["(SERVER=127.0.0.1)", "(PORT=7788)"]

    # 1. Standard ReAction & Drawer
    im_norm, im_sel = render_circular_icon_pair(base_png, badge=None)
    write_amiga_info(im_norm, im_sel, WBTOOL, os.path.join(out_dir, "VortexBrowser.info"), tooltypes=default_tt)
    if include_compat:
        write_amiga_info(im_norm, im_sel, WBTOOL, os.path.join(out_dir, "VortexBrowse.info"), tooltypes=default_tt)
    write_amiga_info(im_norm, im_sel, WBDRAWER, os.path.join(out_dir, "VortexBrowser_drawer.info"))

    # 2. AltiVec Edition (with AltiVec badge)
    av_norm, av_sel = render_circular_icon_pair(base_png, badge="AltiVec")
    write_amiga_info(av_norm, av_sel, WBTOOL, os.path.join(out_dir, "VortexBrowser-AltiVec.info"), tooltypes=default_tt)


if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))

    primary_base = os.path.join(script_dir, "icon_base_cyber_globe.png")
    alt_base = os.path.join(script_dir, "icon_base_titanium_orb.png")
    alt_dir = os.path.join(script_dir, "alt-icons")

    print("=== Generating Primary Icons (Cyber-Globe Vortex, 64x64 circular) ===")
    build_icon_set(primary_base, script_dir, include_compat=True)

    print("\n=== Generating Alternative Icons (3D Titanium Glossy Orb, 64x64 circular) ===")
    build_icon_set(alt_base, alt_dir, include_compat=False)
