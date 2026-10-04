"""Independent, DLL-free pixel, order and timing verifier for saved live evidence."""
import argparse
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def nearest(v):
    return math.floor(v + .5) if v >= 0 else math.ceil(v - .5)


def verify(root):
    params = json.loads((root / 'frame.json').read_text())
    before = (root / 'frame-before.raw').read_bytes()
    after = (root / 'frame-after.raw').read_bytes()
    bpp = params['bytes_per_pixel']
    assert len(before) == len(after) == 640 * 480 * bpp
    mismatch = changed = outside_mismatch = 0
    cx, cy = params['center_x'], params['center_y']
    r, e, core = params['radius'], params['einstein_radius'], params['core_radius']
    for y in range(480):
        for x in range(640):
            dx, dy = x - cx, y - cy
            q = dx * dx + dy * dy
            scale = 1 - e * e * (1 - q / (r * r)) ** 2 / (q + core * core) if q < r*r else 1
            sx = max(0, min(639, cx + nearest(max(-2048, min(2048, dx * scale))))) if abs(dx) <= r and abs(dy) <= r else x
            sy = max(0, min(479, cy + nearest(max(-2048, min(2048, dy * scale))))) if abs(dx) <= r and abs(dy) <= r else y
            dst, src = (y * 640 + x) * bpp, (sy * 640 + sx) * bpp
            mismatch += after[dst:dst+bpp] != before[src:src+bpp]
            changed += after[dst:dst+bpp] != before[dst:dst+bpp]
            if q >= r*r:
                outside_mismatch += after[dst:dst+bpp] != before[dst:dst+bpp]
    rows = []
    ignored = 0
    for row in csv.DictReader((root / 'metrics.csv').read_text().splitlines()):
        try:
            parsed = {k: float(v) for k, v in row.items()}
            if len(parsed) != 6 or any(not math.isfinite(v) for v in parsed.values()):
                raise ValueError()
            rows.append(parsed)
        except (TypeError, ValueError):
            ignored += 1  # abrupt process termination may leave one partial tail
    segments = [[]]
    for row in rows:
        if segments[-1] and row['present_frame'] != segments[-1][-1]['present_frame'] + 1:
            segments.append([])
        segments[-1].append(row)
    measured = max(segments, key=len)[120:]  # omit explicit first-frame QA / warmup
    intervals = [row['interval_ms'] for row in measured]
    costs = [row['effect_ms'] for row in measured]
    def stats(values):
        return dict(mean_ms=statistics.mean(values), p99_ms=sorted(values)[math.ceil(len(values)*.99)-1], max_ms=max(values))
    result = dict(params=params, compared_pixels=640*480, mapping_mismatch=mismatch,
                  changed_pixels=changed, outside_radius_mismatch=outside_mismatch,
                  timed_samples=len(measured), ignored_incomplete_rows=ignored,
                  interval=stats(intervals), effect=stats(costs),
                  average_render_fps=1000 / statistics.mean(intervals),
                  intervals_over_16_6667_ms=sum(v > 1000/60 for v in intervals),
                  fps_scope='one warped render per consecutive presentation ordinal; QPC immediately before warp, 120 warmup frames omitted',
                  timing_position='sorted sprite boundary for layer mode; before presentation for composite mode')
    if (root / 'order.csv').exists():
        expected = list(csv.DictReader((root / 'expected-order.csv').open()))
        actual = list(csv.DictReader((root / 'order.csv').open()))
        result['order_identical'] = expected == [{k: r[k] for k in ('index','key','type')} for r in actual]
        result['node_mutations'] = sum(r['node_unchanged'] != '1' for r in actual)
        result['foreground_compared_pixels'] = sum(int(r['foreground_changed']) for r in actual)
        result['foreground_mismatch'] = sum(int(r['foreground_mismatch']) for r in actual)
        result['foreground_scope'] = 'first-frame opaque KFM raster pixels changed in unwarped control; not all blending modes'
        result['draw_keys'] = [int(r['key']) for r in actual]
    result['hashes'] = {p.name: sha(p) for p in sorted(root.glob('*.raw'))}
    try:
        from PIL import Image
        # RGB565 preview only. Acceptance checks compare untouched packed bytes.
        def preview(data):
            rgb = bytearray()
            for i in range(0, len(data), 2):
                v = data[i] | data[i+1] << 8
                rgb.extend((((v>>11)&31)*255//31, ((v>>5)&63)*255//63, (v&31)*255//31))
            return Image.frombytes('RGB', (640,480), bytes(rgb))
        if params['depth'] == 16:
            for name in ('frame-before','frame-after','queue-control','queue-lensed','final-present'):
                p = root / (name + '.raw')
                if p.exists():
                    preview(p.read_bytes()).save(root / (name + '.png'))
            names = ['queue-control','queue-lensed'] if (root/'queue-control.raw').exists() else ['frame-before','frame-after']
            combined = Image.new('RGB', (1280,480))
            for i,name in enumerate(names):
                combined.paste(Image.open(root/(name+'.png')), (i*640,0))
            combined.save(root/'comparison.png')
    except ImportError:
        pass
    (root / 'results.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    assert mismatch == outside_mismatch == 0 and changed > 0
    if 'order_identical' in result:
        assert result['order_identical'] and result['node_mutations'] == result['foreground_mismatch'] == 0
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('evidence', type=Path)
    verify(parser.parse_args().evidence)
