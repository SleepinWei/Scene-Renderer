#!/usr/bin/env python3
"""Plot actual --render-gallery DIR diagnostics readbacks (NumPy + Pillow).

False colors decode captured data; beauty images are only resized/cropped.
This script neither generates textures nor changes the captured renderer output.
"""
import argparse
import json
from pathlib import Path
import shutil

import numpy as np
from PIL import Image, ImageDraw, ImageFont

COLORS = np.array([[71, 197, 148], [73, 166, 232], [246, 197, 75],
                   [239, 126, 83], [169, 119, 224], [231, 113, 169], [145, 162, 179]], dtype=np.uint8)
BG = (17, 23, 32)


def font(size):
    for path in ("/System/Library/Fonts/Supplemental/Arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            pass
    return ImageFont.load_default()


def raw(path, dtype, shape):
    values = np.fromfile(path, dtype=dtype)
    if values.size != int(np.prod(shape)):
        raise ValueError(f"Invalid readback dimensions: {path}")
    if not np.isfinite(values).all():
        raise ValueError(f"Nonfinite readback: {path}")
    return values.reshape(shape)


def panel(image, title, detail, width=550, height=420, nearest=False):
    canvas = Image.new("RGB", (width, height), BG)
    draw = ImageDraw.Draw(canvas)
    draw.text((16, 12), title, fill="white", font=font(22))
    draw.text((16, 43), detail, fill=(175, 193, 209), font=font(15))
    image = image.convert("RGB")
    ratio = min((width-32)/image.width, (height-92)/image.height)
    image = image.resize((round(image.width*ratio), round(image.height*ratio)), Image.Resampling.NEAREST if nearest else Image.Resampling.LANCZOS)
    canvas.paste(image, ((width-image.width)//2, 78+(height-92-image.height)//2))
    return canvas


def grid(panels, columns, title, footer, path):
    w = panels[0].width
    rows = (len(panels)+columns-1)//columns
    heights = [max(item.height for item in panels[row*columns:(row+1)*columns]) for row in range(rows)]
    canvas = Image.new("RGB", (columns*w, sum(heights)+92), BG)
    draw = ImageDraw.Draw(canvas)
    draw.text((18, 12), title, fill="white", font=font(27))
    for i, item in enumerate(panels):
        canvas.paste(item, ((i % columns)*w, 52+sum(heights[:i//columns])))
    draw.text((18, canvas.height-29), footer, fill=(175, 193, 209), font=font(15))
    canvas.save(path)


def vt_data(directory, name, kind, info):
    prefix = directory/f"{name}-vt-{kind}"
    table = raw(f"{prefix}-table.f32", "<f4", (info["table_height"], info["table_width"], 4))
    levels, row = [], 0
    for mip in range(info["max_mip"]+1):
        n = info["table_width"] >> mip
        levels.append(table[row:row+n, :n])
        row += n
    entries = np.concatenate([level.reshape(-1, 4) for level in levels])
    resident = entries[entries[:, 3] > .5]
    if len(resident) != info["resident"] or levels[-1][0, 0, 3] < .5:
        raise ValueError("VT residency count differs from GPU page table or root is missing")
    if len(set(map(tuple, resident[:, :2].astype(int)))) != len(resident):
        raise ValueError("Two resident pages share a physical slot")
    extent, pitch = info["atlas_extent"], info["pitch"]
    occupied = np.zeros((extent, extent), dtype=bool)
    for x, y, _, _ in resident.astype(int):
        occupied[y*pitch:(y+1)*pitch, x*pitch:(x+1)*pitch] = True
    atlases = []
    for layer in range(info["layers"]):
        dtype, ext = ("<f4", "f32") if kind == "height" else ("u1", "u8")
        data = raw(f"{prefix}-atlas-{layer}.{ext}", dtype, (extent, extent, 4))
        if kind == "height":
            data = np.repeat(np.clip((data[:, :, :1]-info["minimum"])/max(info["maximum"]-info["minimum"], 1e-8), 0, 1), 3, axis=2)*255
        rgb = data[:, :, :3].astype(np.uint8).copy()
        rgb[~occupied] = (31, 40, 53)
        # Thin lines mark physical page boundaries; the 2-texel aprons remain visible.
        rgb[::pitch, :] = (100, 119, 133)
        rgb[:, ::pitch] = (100, 119, 133)
        atlases.append(Image.fromarray(rgb))
    return levels, atlases


def page_grid(levels):
    canvas = Image.new("RGB", (500, 330), BG)
    draw = ImageDraw.Draw(canvas)
    for mip, level in enumerate(levels):
        n = level.shape[0]
        x = 10+mip*98 if mip else 10
        if mip == 0:
            x, y, size = 10, 34, 250
        else:
            x, y, size = 280+((mip-1) % 2)*100, 34+((mip-1)//2)*102, 82
        color = np.full((n, n, 3), (31, 40, 53), dtype=np.uint8)
        color[level[:, :, 3] > .5] = COLORS[mip]
        image = Image.fromarray(color).resize((size, size), Image.Resampling.NEAREST)
        canvas.paste(image, (x, y))
        draw.text((x, y-21), f"mip {mip} ({n}x{n})", font=font(14), fill="white")
        for i in range(n+1):
            at = round(i*size/n)
            draw.line((x+at, y, x+at, y+size), fill=(87, 99, 114))
            draw.line((x, y+at, x+size, y+at), fill=(87, 99, 114))
    return canvas


def fallback_map(levels, size=512, height_field=False):
    v, u = np.meshgrid((np.arange(size)+.5)/size, (np.arange(size)+.5)/size, indexing="ij")
    selected = np.full((size, size), len(levels)-1, dtype=int)
    for mip in reversed(range(len(levels))):
        level = levels[mip]
        n = level.shape[0]
        dimension = n*64
        px = u*(dimension-1) if height_field else u*dimension-.5
        py = v*(dimension-1) if height_field else v*dimension-.5
        x = np.clip(np.floor(px/64).astype(int), 0, n-1)
        y = np.clip(np.floor(py/64).astype(int), 0, n-1)
        found = level[y, x, 3] > .5
        selected[found] = mip
    return Image.fromarray(COLORS[selected]), {str(m): int((selected == m).sum()) for m in range(len(levels))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--crop", nargs=4, type=int, default=[470, 390, 700, 540], metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"))
    args = parser.parse_args()
    source, output = args.capture, args.output
    output.mkdir(parents=True, exist_ok=True)
    terrain = json.loads((source/"terrain-diagnostics.json").read_text())
    shadow = json.loads((source/"shadow-test-diagnostics.json").read_text())
    hl, ha = vt_data(source, "terrain", "height", terrain["vt"]["height"])
    ml, ma = vt_data(source, "terrain", "material", terrain["vt"]["material"])
    grid([panel(ha[0], "Height physical cache", "GPU RGBA32Float / normalized height"),
          panel(ma[0], "Albedo physical cache", "GPU RGBA8 / encoded base color"),
          panel(ma[1], "Normal physical cache", "This procedural scene uses flat tangent normals"),
          panel(ma[3], "Roughness physical cache", "Uniform roughness in this procedural scene")],
         2, "Virtual Texture | actual GPU physical caches", "64x64 content + 2-texel apron on each side; grid marks 68x68 slots.", output/"vt-cache.png")
    hf, hs = fallback_map(hl, height_field=True)
    mf, ms = fallback_map(ml)
    panels = []
    for kind, levels, view, info in (("Height", hl, hf, terrain["vt"]["height"]), ("Material", ml, mf, terrain["vt"]["material"])):
        panels.append(panel(page_grid(levels), f"{kind}: actual page table", f"{info['resident']}/{info['capacity']} slots / {info['extent']}x{info['extent']} virtual texels", nearest=True))
    panels += [panel(hf, "Height: first resident ancestor", "Full virtual UV domain / request mip 0", nearest=True),
               panel(mf, "Material: first resident ancestor", "CPU decode of captured table / request mip 0", nearest=True)]
    grid(panels, 2, "Virtual Texture | residency and fallback", "Mip colors: 0 green / 1 blue / 2 yellow / 3 orange / 4 purple. Dark table cells = absent.", output/"vt-residency.png")
    w, h = shadow["width"], shadow["height"]
    positions = raw(source/"shadow-test-positions.f32", "<f4", (h, w, 4))
    view = np.array(shadow["camera_view"])
    distance = -(positions @ view.T)[:, :, 2]
    sun = next(light for light in shadow["shadow"]["lights"] if light["type"] == 0)
    splits = np.array(sun["splits"]+[shadow["shadow"]["distance"]])
    cascade = np.searchsorted(splits[:4], distance, side="left")
    rgb = COLORS[cascade].astype(float)
    for i in range(4):
        start = shadow["shadow"]["camera_near"] if i == 0 else splits[i-1]
        band = (splits[i]-start)*shadow["shadow"]["blend_fraction"]
        t = np.clip((distance-(splits[i]-band))/max(band, 1e-8), 0, 1)
        t = t*t*(3-2*t)
        at = cascade == i
        rgb[at] = (1-t[at, None])*COLORS[i] + t[at, None]*COLORS[i+1]
    far_start = (1-shadow["shadow"]["fade_fraction"])*splits[-1]+shadow["shadow"]["fade_fraction"]*splits[-2]
    fade = np.clip((distance-far_start)/(splits[-1]-far_start), 0, 1)
    fade = fade*fade*(3-2*fade)
    rgb = (1-fade[:, :, None])*rgb+fade[:, :, None]*np.array([100, 111, 126])
    valid = (positions[:, :, 3] > .5) & (distance >= 0)
    rgb[~valid] = BG
    grid([panel(Image.open(source/"shadow-test.png"), "PCSS: default solar radius", "Same captured camera and geometry"),
          panel(Image.fromarray(rgb.astype(np.uint8)), "CSM: receiver cascade + overlap", "CPU classification of GPU world-position G-buffer")],
         2, "Cascaded shadows | five camera-depth slices", "Ends (meters): "+" / ".join(f"{s:.2f}" for s in splits)+". Gray = final fade / outside shadow distance.", output/"csm-cascades.png")
    extent = shadow["shadow"]["extent"]
    atlas = raw(source/"shadow-test-shadow.f32", "<f4", (extent, extent))
    values = atlas[atlas < .99999]
    if not values.size:
        raise ValueError("Shadow atlas contains no caster depth")
    low, high = float(values.min()), float(values.max())
    grayscale = np.uint8(np.clip((atlas-low)/max(high-low, 1e-8), 0, 1)*220+25)
    image = Image.fromarray(np.repeat(grayscale[:, :, None], 3, axis=2))
    draw = ImageDraw.Draw(image)
    tiles = []
    for i, tile in enumerate(sun["tiles"]):
        x, y, tw, th = np.array(tile["rect"])*extent
        rect = tuple(round(v) for v in (x, y, x+tw, y+th))
        draw.rectangle(rect, outline=tuple(COLORS[i]), width=7)
        draw.text((rect[0]+14, rect[1]+14), f"C{i}", font=font(42), fill=tuple(COLORS[i]))
        tiles.append(panel(image.crop(rect), f"Cascade {i}", f"Ends at {splits[i]:.2f} m", width=330, height=365))
    grid([panel(image, "Actual shadow depth atlas", f"{extent}x{extent} Depth32Float / one sun, five tiles", width=900, height=760)],
         1, "CSM | captured depth attachment", f"Grayscale stretch: {low:.5f} .. {high:.5f}; cleared depth 1 is white; outlines identify tiles.", output/"csm-atlas.png")
    grid(tiles, 5, "CSM | individual depth tiles", "One global depth grayscale range for all five tiles; these are light-space projections, not camera views.", output/"csm-tiles.png")
    comparison = [("shadow-test-pcf.png", "PCF", "Fixed 3x3 comparison filter"),
                  ("shadow-test.png", "PCSS / solar radius", "Angular radius 0.00465 rad (default)"),
                  ("shadow-test-pcss-wide.png", "PCSS / enlarged emitter", "Angular radius 0.04 rad (illustration)")]
    panels = [panel(Image.open(source/file), title, detail, width=500, height=440) for file, title, detail in comparison]
    panels += [panel(Image.open(source/file).crop(args.crop), title+" / identical crop", "Receiver shadow detail", width=500, height=280) for file, title, _ in comparison]
    grid(panels, 3, "PCF versus PCSS | controlled directional-light scene", "Same camera / material / sun direction; 0.04 rad is deliberately enlarged, not the default Sun.", output/"pcss-comparison.png")
    pcf = np.asarray(Image.open(source/"shadow-test-pcf.png").convert("RGB"), dtype=float)
    default = np.asarray(Image.open(source/"shadow-test.png").convert("RGB"), dtype=float)
    wide = np.asarray(Image.open(source/"shadow-test-pcss-wide.png").convert("RGB"), dtype=float)
    ground = valid & (np.abs(positions[:, :, 1]) < .01)
    summary = {"terrain": terrain, "shadow": shadow, "fallback_uv_pixel_counts": {"height": hs, "material": ms},
               "cascade_receiver_pixels": {str(i): int((valid & (cascade == i) & (distance < splits[-1])).sum()) for i in range(5)},
               "comparison_crop": args.crop, "ground_difference_8bit": {
                   "default_pcss_vs_pcf_mean_absolute_rgb": float(np.abs(default-pcf)[ground].mean()),
                   "wide_pcss_vs_pcf_mean_absolute_rgb": float(np.abs(wide-pcf)[ground].mean()),
                   "default_changed_pixels_gt_1": int((ground & (np.abs(default-pcf).max(axis=2) > 1)).sum()),
                   "wide_changed_pixels_gt_1": int((ground & (np.abs(wide-pcf).max(axis=2) > 1)).sum())}}
    (output/"capture-summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    for file in ("terrain.png", "terrain-wireframe.png", "shadow-test.png"):
        shutil.copyfile(source/file, output/file)
    print(json.dumps({key: summary[key] for key in ("cascade_receiver_pixels", "ground_difference_8bit")}, indent=2))


if __name__ == "__main__":
    main()
