#!/usr/bin/env python3
"""Cut the runtime's PS2_GS_DRAWSEQ=N:K stream into game frames and compare them pass by pass with a PCSX2 frame.
   drawseq_frames.py <run.log> <pcsx2 passes.txt> [display fbp, default 232]
The PS2_GS_FRAMESUM / PS2_GS_DRAWSEQ blocks follow the host presents, which do not line up with the game's frames (a
block can hold half a frame or one and a half), so per-present counts swing. Here a frame starts at the pass that
copies into the display buffer (FBP of DISPFB, 232 in the GoW2 menu), as a PCSX2 dump segment does, and consecutive
passes with the same key are merged (the probe cuts the pass running at each present). <pcsx2 passes.txt> is the output
of gs_passes.py <dump> <segment>. Prints the drawn strip kicks (verts - adc) per frame and, for one frame with the same
pass count as PCSX2, every pass whose vertex or ADC count differs."""
import collections, re, sys

PAT = re.compile(r'#\s*\d+ prim=(\w+)\(.*?\) fbp=(\d+) fbw=\d+ fbmsk=(\w+) tbp=(-?\d+)(?: cbp=(\d+))? alpha=(\w+) test=(\w+) '
                 r'zbp=\d+ verts=(\d+) adc=(\d+)')
STRIPS = ('05c', '154', '0d4', '05d', '25c', '35c')


def parse(lines):
    out = []
    for ln in lines:
        m = PAT.search(ln)
        if not m:
            continue
        p, fbp, msk, tbp, cbp, al, te, v, a = m.groups()
        key = (p, int(fbp), msk, int(tbp), int(cbp or 0), al, te)
        if out and out[-1][0] == key:
            out[-1][1] += int(v); out[-1][2] += int(a)
        else:
            out.append([key, int(v), int(a)])
    return out


def drawn(frame):
    return sum(p[1] - p[2] for p in frame if p[0][0] in STRIPS)


def main():
    disp = int(sys.argv[3]) if len(sys.argv) > 3 else 232
    frames, cur = [], None
    for p in parse(l for l in open(sys.argv[1], errors='replace') if l.startswith('[drawseq] #')):
        if p[0][1] == disp:
            if cur is not None:
                frames.append(cur)
            cur = []
        if cur is not None:
            cur.append(p)
    ref = parse(open(sys.argv[2]))
    if not frames:
        sys.exit('no complete frame (is the display FBP right?)')
    counts = sorted(collections.Counter(len(f) for f in frames).items())
    print(f'{len(frames)} frames, passes per frame {counts}, drawn kicks per frame {min(map(drawn, frames))}-{max(map(drawn, frames))}'
          f' | PCSX2: {len(ref)} passes, {drawn(ref)} drawn')
    same = [f for f in frames if len(f) == len(ref)]
    f = same[len(same) // 2] if same else frames[0]
    total = 0
    for i, (a, b) in enumerate(zip(ref, f)):
        if (a[1], a[2]) != (b[1], b[2]) or a[0][0] != b[0][0] or a[0][5] != b[0][5]:
            da, db = a[1] - a[2], b[1] - b[2]
            total += db - da
            print(f'#{i:<3d} tbp {a[0][3]:5d}/{b[0][3]:<5d} cbp {a[0][4]:5d}/{b[0][4]:<5d} alpha {a[0][5][-4:]}/{b[0][5][-4:]} '
                  f'verts {a[1]:5d}/{b[1]:<5d} adc {a[2]:5d}/{b[2]:<5d} drawn {da:5d}/{db:<5d} ({db - da:+d})')
    print(f'sum of the differences (ours - PCSX2): {total:+d}')


if __name__ == '__main__':
    main()
