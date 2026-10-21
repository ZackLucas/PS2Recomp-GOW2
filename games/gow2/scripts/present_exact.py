#!/usr/bin/env python3
"""Pixel-by-pixel comparison of presented frames with a PCSX2 frame (same size, raw, before any window scaling).
   present_exact.py <ref.ppm> <frame.ppm ...> [--out DIR] [--json FILE]
Ref: `gow2_gs_replay <dump> --vram-offset 425 --out P` writes Pinit.ppm, the frame PCSX2 left in VRAM.
Frames: the runtime's PS2_GS_PRESENT_DUMP=N writes present_NNNNNN.ppm.
Per frame: mean absolute error (RGB, 0-255) and % of pixels whose largest channel differs by more than 8,
also between consecutive frames (the animation floor). For the frame closest to the ref: the error on a grid
of 64x64 tiles and, with --out, a sheet ref | frame | |diff| x4."""
import json, os, sys
from PIL import Image, ImageChops

TILE = 64


def load(path):
    return Image.open(path).convert('RGB')


def diff(a, b):
    d = ImageChops.difference(a, b)
    hist = d.histogram()
    n = a.size[0] * a.size[1]
    mean = sum(i * c for ch in range(3) for i, c in enumerate(hist[ch * 256:(ch + 1) * 256])) / (3 * n)
    r, g, bl = d.split()
    largest = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    over8 = sum(largest.histogram()[9:]) * 100.0 / n
    return {'mean_abs': round(mean, 3), 'over8_pct': round(over8, 2)}


def main():
    args, out_dir, json_path = [], None, None
    it = iter(sys.argv[1:])
    for a in it:
        if a == '--out':
            out_dir = next(it)
        elif a == '--json':
            json_path = next(it)
        else:
            args.append(a)
    if len(args) < 2:
        sys.exit(__doc__)
    ref = load(args[0])
    frames = [(p, load(p)) for p in args[1:]]
    rows = []
    for p, im in frames:
        if im.size != ref.size:
            print('%s: size %s != ref %s' % (p, im.size, ref.size))
            continue
        rows.append(dict(frame=os.path.basename(p), **diff(ref, im)))
        print('%-24s vs ref: mean_abs=%6.2f  >8: %5.1f%%' % (rows[-1]['frame'], rows[-1]['mean_abs'], rows[-1]['over8_pct']))
    floor = []
    for (pa, a), (pb, b) in zip(frames, frames[1:]):
        if a.size == b.size:
            floor.append(dict(a=os.path.basename(pa), b=os.path.basename(pb), **diff(a, b)))
            print('%-24s vs %-24s mean_abs=%6.2f  >8: %5.1f%%' % (floor[-1]['a'], floor[-1]['b'], floor[-1]['mean_abs'], floor[-1]['over8_pct']))
    best = min(rows, key=lambda r: r['mean_abs'])
    best_im = dict(frames)[next(p for p, _ in frames if os.path.basename(p) == best['frame'])]
    w, h = ref.size
    tiles = []
    for ty in range(0, h, TILE):
        for tx in range(0, w, TILE):
            box = (tx, ty, min(w, tx + TILE), min(h, ty + TILE))
            tiles.append(dict(x=tx, y=ty, **diff(ref.crop(box), best_im.crop(box))))
    tiles.sort(key=lambda t: -t['mean_abs'])
    print('best %s: mean_abs=%.2f >8: %.1f%%; worst tiles (x,y mean):' % (best['frame'], best['mean_abs'], best['over8_pct']),
          ' '.join('(%d,%d %.1f)' % (t['x'], t['y'], t['mean_abs']) for t in tiles[:6]))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        sheet = Image.new('RGB', (w * 3 + 16, h), (24, 24, 24))
        sheet.paste(ref, (0, 0))
        sheet.paste(best_im, (w + 8, 0))
        sheet.paste(ImageChops.difference(ref, best_im).point(lambda v: min(255, v * 4)), (2 * w + 16, 0))
        sheet.save(os.path.join(out_dir, 'present_exact_%s.png' % os.path.splitext(best['frame'])[0]))
    if json_path:
        with open(json_path, 'w') as f:
            json.dump(dict(ref=os.path.basename(args[0]), size=list(ref.size), frames=rows, consecutive=floor,
                           best=best, best_worst_tiles=tiles[:8]), f, indent=1)


if __name__ == '__main__':
    main()
