#!/usr/bin/env python3
"""Turn a walked centerline into the two drawings the design page needs.

Reads   final_centerline.csv  (distance_m, x_m, y_m, elevation_m, segment_index)
        final.json            (segments, corners, measured, ...)

Emits   circuit_svg.json      { "map": {...}, "profile": {...} }

Everything the page draws comes from the same walk that produced the measured
numbers, so the map and the corner table cannot disagree. Nothing here is
hand-authored path data.
"""

import csv
import json
import math
import os
import sys

DIR = os.environ.get('CIRCUIT_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'docs', 'circuits'))


def read_centerline(path):
    rows = []
    with open(path, newline='') as f:
        for r in csv.DictReader(f):
            rows.append({
                'd': float(r['distance_m']),
                'x': float(r['x_m']),
                'y': float(r['y_m']),
                'z': float(r['elevation_m']),
                'seg': int(float(r['segment_index'])),
            })
    return rows


def fit(rows, width, height, pad):
    """Map world metres to SVG units, Y flipped, aspect preserved."""
    xs = [r['x'] for r in rows]
    ys = [r['y'] for r in rows]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    span_x = max(x1 - x0, 1e-6)
    span_y = max(y1 - y0, 1e-6)
    s = min((width - 2 * pad) / span_x, (height - 2 * pad) / span_y)
    ox = pad + ((width - 2 * pad) - span_x * s) / 2
    oy = pad + ((height - 2 * pad) - span_y * s) / 2

    def to_svg(x, y):
        return (ox + (x - x0) * s, height - (oy + (y - y0) * s))

    return to_svg, s, (x0, y0, x1, y1)


def offset_path(rows, to_svg, half_width_m, scale, side):
    """The asphalt edge, offset from the centerline by the local normal."""
    pts = []
    n = len(rows)
    for i, r in enumerate(rows):
        a = rows[(i - 1) % n]
        b = rows[(i + 1) % n]
        tx, ty = b['x'] - a['x'], b['y'] - a['y']
        L = math.hypot(tx, ty) or 1.0
        nx, ny = ty / L * side, -tx / L * side
        pts.append(to_svg(r['x'] + nx * half_width_m, r['y'] + ny * half_width_m))
    return pts


def to_d(pts, close=True):
    head = 'M %.2f %.2f' % pts[0]
    body = ' '.join('L %.2f %.2f' % p for p in pts[1:])
    return head + ' ' + body + (' Z' if close else '')


def seg_width(layout, idx, default=8.0):
    for s in layout.get('segments', []):
        if int(s.get('index', -1)) == idx:
            return float(s.get('width_m', default) or default)
    return default


def main():
    csv_path = os.path.join(DIR, os.environ.get('CIRCUIT_STEM','valdirone_nuova') + '_centerline.csv')
    json_path = os.path.join(DIR, os.environ.get('CIRCUIT_STEM','valdirone_nuova') + '.json')
    if not os.path.exists(csv_path):
        sys.exit('no centerline at ' + csv_path)

    rows = read_centerline(csv_path)
    layout = json.load(open(json_path)) if os.path.exists(json_path) else {}

    W, H, PAD = 1000.0, 720.0, 48.0
    to_svg, scale, bbox = fit(rows, W, H, PAD)

    # Track ribbon: two offset edges. Width varies per segment, which is the
    # whole point of widening a passing corner, so it is read per sample.
    widths = [seg_width(layout, r['seg']) / 2.0 for r in rows]
    left, right = [], []
    n = len(rows)
    for i, r in enumerate(rows):
        a, b = rows[(i - 1) % n], rows[(i + 1) % n]
        tx, ty = b['x'] - a['x'], b['y'] - a['y']
        L = math.hypot(tx, ty) or 1.0
        nx, ny = ty / L, -tx / L
        hw = widths[i]
        left.append(to_svg(r['x'] + nx * hw, r['y'] + ny * hw))
        right.append(to_svg(r['x'] - nx * hw, r['y'] - ny * hw))

    ribbon = to_d(left, False) + ' ' + to_d(list(reversed(right)), False) + ' Z'
    center = to_d([to_svg(r['x'], r['y']) for r in rows])

    # Corner markers, placed at the mid-arc sample of each corner's segments.
    markers = []
    for c in layout.get('corners', []):
        idxs = c.get('segment_indices') or []
        member = [r for r in rows if r['seg'] in idxs]
        if not member:
            continue
        m = member[len(member) // 2]
        sx, sy = to_svg(m['x'], m['y'])
        markers.append({
            'name': c.get('name', '?'),
            'x': round(sx, 1), 'y': round(sy, 1),
            'd': round(m['d'], 1),
            'radius_m': c.get('radius_m'),
            'apex_kmh': c.get('apex_kmh'),
            'hand': c.get('hand'),
            'gear': c.get('gear'),
        })

    sx, sy = to_svg(rows[0]['x'], rows[0]['y'])
    a, b = rows[1], rows[-2]
    hx, hy = a['x'] - b['x'], a['y'] - b['y']
    heading = math.degrees(math.atan2(hx, hy))

    # Scale bar: a round number of metres in SVG units.
    for candidate in (200, 100, 50):
        if candidate * scale < (W - 2 * PAD) * 0.35:
            bar_m = candidate
            break
    else:
        bar_m = 50

    # Elevation profile, its own coordinate space.
    PW, PH, PPAD = 1000.0, 220.0, 28.0
    zs = [r['z'] for r in rows]
    z0, z1 = min(zs), max(zs)
    zspan = max(z1 - z0, 0.5)
    dmax = rows[-1]['d'] or 1.0

    def to_prof(d, z):
        return (PPAD + (d / dmax) * (PW - 2 * PPAD),
                PH - PPAD - ((z - z0) / zspan) * (PH - 2 * PPAD))

    prof = [to_prof(r['d'], r['z']) for r in rows]
    prof_line = to_d(prof, False)
    prof_fill = prof_line + ' L %.2f %.2f L %.2f %.2f Z' % (
        prof[-1][0], PH - PPAD, prof[0][0], PH - PPAD)

    prof_markers = []
    for m in markers:
        r = min(rows, key=lambda q: abs(q['d'] - m['d']))
        px, py = to_prof(r['d'], r['z'])
        prof_markers.append({'name': m['name'], 'x': round(px, 1),
                             'y': round(py, 1), 'z': round(r['z'], 2)})

    out = {
        'map': {
            'width': W, 'height': H,
            'ribbon': ribbon, 'center': center,
            'start': {'x': round(sx, 1), 'y': round(sy, 1),
                      'heading_deg': round(heading, 1)},
            'markers': markers,
            'scale_px_per_m': round(scale, 4),
            'scale_bar_m': bar_m,
            'scale_bar_px': round(bar_m * scale, 1),
            'bbox_m': [round(v, 1) for v in bbox],
            'extent_m': [round(bbox[2] - bbox[0], 1), round(bbox[3] - bbox[1], 1)],
        },
        'profile': {
            'width': PW, 'height': PH,
            'line': prof_line, 'fill': prof_fill,
            'markers': prof_markers,
            'z_min': round(z0, 2), 'z_max': round(z1, 2),
            'z_range': round(z1 - z0, 2),
            'd_max': round(dmax, 1),
        },
        'samples': len(rows),
    }
    dest = os.path.join(DIR, os.environ.get('CIRCUIT_STEM','valdirone_nuova') + '_svg.json')
    json.dump(out, open(dest, 'w'), indent=1)
    print('wrote %s  (%d samples, %.1f m lap, %.2f m elevation range)'
          % (dest, len(rows), dmax, z1 - z0))
    print('map extent %.0f x %.0f m, %.4f px/m'
          % (out['map']['extent_m'][0], out['map']['extent_m'][1], scale))


if __name__ == '__main__':
    main()
