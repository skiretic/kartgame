#!/usr/bin/env python3
"""Driver fit, measured off the exported glb -- no Blender, no engine.

ADR-0055 item 6: gate 3's numbers must be reproducible from a command, not
from a scratch directory. This is the promotion of the session-temporary
`measure_presses.py` harness that adjudicated #202's foot chain (0.20 mm where
the gate said 0.20 mm), widened to the whole driver:

  - every `joints.DRIVER_CONTACTS` pair: minimum vertex-to-face distance both
    directions, judged against `CONTACT_TOLERANCE` -- a declared contact that
    does not touch is a FAIL and a nonzero exit;
  - every gate="driver" waiver in `joints.OPEN_DEFECTS`: penetration depth by
    ray-crossing parity, the same measurement `genkart.driver_depth` makes,
    including the ADR-0057 rake-plane clip on `driver_torso` findings.

The glb is the **low-detail export**, so figures here can differ from the
gate's high-detail ones by a bevel; what must agree is the sign and the order
of magnitude. Run after `genkart.sh`:

    python3 tools/verify/driver_fit.py
    python3 tools/verify/driver_fit.py --pair=driver_boot_l:pedal_brake_pad

Coordinate note that has cost a wrong conclusion before: the exporter maps
kart (x, y, z) to glb (x, z, -y), so kart y = -glb z and kart z = glb y. The
rake plane below is stated in kart coordinates and converted at the sample.
"""

from __future__ import annotations

import fnmatch
import json
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools", "blender"))
from kartlib import joints  # noqa: E402

GLB = os.path.join(REPO, "assets", "generated", "kart.glb")

# ADR-0057: y(z) = -365 + 0.404 * (365 - z), millimeters, kart coordinates.
RAKE_PLANE_ANCHOR = 0.365
RAKE_PLANE_SLOPE = 0.404
_RAKE_NORM = (1.0 + RAKE_PLANE_SLOPE * RAKE_PLANE_SLOPE) ** 0.5


def rake_clearance_glb(point):
    """Signed distance forward of the rake plane for a glb-space point, meters."""
    kart_y = -point[2]
    kart_z = point[1]
    plane_y = -RAKE_PLANE_ANCHOR + RAKE_PLANE_SLOPE * (RAKE_PLANE_ANCHOR - kart_z)
    return (kart_y - plane_y) / _RAKE_NORM


def load_glb(path):
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:4] != b"glTF":
        raise SystemExit("driver_fit: %s is not a glb" % path)
    length = struct.unpack("<I", data[12:16])[0]
    gltf = json.loads(data[20 : 20 + length])
    offset = 20 + length
    binchunk = b""
    while offset < len(data):
        clen = struct.unpack("<I", data[offset : offset + 4])[0]
        if data[offset + 4 : offset + 8] == b"BIN\x00":
            binchunk = data[offset + 8 : offset + 8 + clen]
        offset += 8 + clen
    return gltf, binchunk


def accessor(gltf, binchunk, index):
    acc = gltf["accessors"][index]
    view = gltf["bufferViews"][acc["bufferView"]]
    start = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
    comps = {"SCALAR": 1, "VEC2": 2, "VEC3": 3}[acc["type"]]
    fmt = {5126: "f", 5125: "I", 5123: "H"}[acc["componentType"]]
    size = struct.calcsize(fmt) * comps
    return [
        struct.unpack_from("<" + fmt * comps, binchunk, start + i * size)
        for i in range(acc["count"])
    ]


def _local_matrix(node):
    if "matrix" in node:
        m = node["matrix"]
        return [
            [m[0], m[4], m[8], m[12]],
            [m[1], m[5], m[9], m[13]],
            [m[2], m[6], m[10], m[14]],
            [0.0, 0.0, 0.0, 1.0],
        ]
    t = node.get("translation", [0.0, 0.0, 0.0])
    q = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    s = node.get("scale", [1.0, 1.0, 1.0])
    x, y, z, w = q
    r = [
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ]
    return [[r[i][j] * s[j] for j in range(3)] + [t[i]] for i in range(3)] + [
        [0.0, 0.0, 0.0, 1.0]
    ]


def _matmul(a, b):
    return [
        [sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)
    ]


class Meshes:
    """Every named mesh node as world-space triangles, loaded once."""

    def __init__(self, gltf, binchunk):
        self.gltf = gltf
        self.binchunk = binchunk
        self.parent_of = {}
        for i, node in enumerate(gltf["nodes"]):
            for child in node.get("children", []):
                self.parent_of[child] = i
        self.by_name = {}
        for i, node in enumerate(gltf["nodes"]):
            if "mesh" in node and node.get("name"):
                self.by_name[node["name"]] = i
        self._tris = {}
        self._bounds = {}

    def names(self):
        return sorted(self.by_name)

    def bounds(self, name):
        if name not in self._bounds:
            points = [p for tri in self.tris(name) for p in tri]
            self._bounds[name] = (
                tuple(min(p[i] for p in points) for i in range(3)),
                tuple(max(p[i] for p in points) for i in range(3)),
            )
        return self._bounds[name]

    def disjoint(self, name_a, name_b):
        low_a, high_a = self.bounds(name_a)
        low_b, high_b = self.bounds(name_b)
        return any(
            low_a[i] > high_b[i] or low_b[i] > high_a[i] for i in range(3)
        )

    def tris(self, name):
        if name in self._tris:
            return self._tris[name]
        index = self.by_name.get(name)
        if index is None:
            raise SystemExit("driver_fit: no mesh node named %s in the glb" % name)
        chain = [index]
        while chain[-1] in self.parent_of:
            chain.append(self.parent_of[chain[-1]])
        world = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        for node_index in reversed(chain):
            world = _matmul(world, _local_matrix(self.gltf["nodes"][node_index]))
        out = []
        node = self.gltf["nodes"][index]
        for prim in self.gltf["meshes"][node["mesh"]]["primitives"]:
            verts = accessor(self.gltf, self.binchunk, prim["attributes"]["POSITION"])
            idx = [i[0] for i in accessor(self.gltf, self.binchunk, prim["indices"])]
            placed = [
                tuple(
                    world[i][0] * v[0] + world[i][1] * v[1] + world[i][2] * v[2]
                    + world[i][3]
                    for i in range(3)
                )
                for v in verts
            ]
            for k in range(0, len(idx), 3):
                out.append((placed[idx[k]], placed[idx[k + 1]], placed[idx[k + 2]]))
        self._tris[name] = out
        return out


def _sub(u, v):
    return (u[0] - v[0], u[1] - v[1], u[2] - v[2])


def _dot(u, v):
    return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]


def _cross(u, v):
    return (
        u[1] * v[2] - u[2] * v[1],
        u[2] * v[0] - u[0] * v[2],
        u[0] * v[1] - u[1] * v[0],
    )


def point_tri_dist2(p, a, b, c):
    """Squared distance point-to-triangle, Ericson's closest-point walk."""
    ab, ac, ap = _sub(b, a), _sub(c, a), _sub(p, a)
    d1, d2 = _dot(ab, ap), _dot(ac, ap)
    if d1 <= 0 and d2 <= 0:
        q = a
    else:
        bp = _sub(p, b)
        d3, d4 = _dot(ab, bp), _dot(ac, bp)
        if d3 >= 0 and d4 <= d3:
            q = b
        else:
            vc = d1 * d4 - d3 * d2
            if vc <= 0 and d1 >= 0 and d3 <= 0:
                v = d1 / (d1 - d3)
                q = (a[0] + ab[0] * v, a[1] + ab[1] * v, a[2] + ab[2] * v)
            else:
                cp = _sub(p, c)
                d5, d6 = _dot(ab, cp), _dot(ac, cp)
                if d6 >= 0 and d5 <= d6:
                    q = c
                else:
                    vb = d5 * d2 - d1 * d6
                    if vb <= 0 and d2 >= 0 and d6 <= 0:
                        w = d2 / (d2 - d6)
                        q = (a[0] + ac[0] * w, a[1] + ac[1] * w, a[2] + ac[2] * w)
                    else:
                        va = d3 * d6 - d5 * d4
                        if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
                            w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
                            bc = _sub(c, b)
                            q = (b[0] + bc[0] * w, b[1] + bc[1] * w, b[2] + bc[2] * w)
                        else:
                            den = 1.0 / (va + vb + vc)
                            v, w = vb * den, vc * den
                            q = (
                                a[0] + ab[0] * v + ac[0] * w,
                                a[1] + ab[1] * v + ac[1] * w,
                                a[2] + ab[2] * v + ac[2] * w,
                            )
    d = _sub(p, q)
    return _dot(d, d)


def min_gap(tris_a, tris_b):
    """Minimum vertex-to-face distance, both directions, meters."""
    best = float("inf")
    for source, target in ((tris_a, tris_b), (tris_b, tris_a)):
        points = set()
        for tri in source:
            points.update(tri)
        for p in points:
            for a, b, c in target:
                d2 = point_tri_dist2(p, a, b, c)
                if d2 < best:
                    best = d2
    return best**0.5


_RAY = (0.017, 1.0, 0.083)  # not axis-aligned, so a grid mesh cannot graze it


def _crossings(point, tris):
    count = 0
    for a, b, c in tris:
        # Moller-Trumbore, one-sided epsilon on the determinant only.
        e1, e2 = _sub(b, a), _sub(c, a)
        pvec = _cross(_RAY, e2)
        det = _dot(e1, pvec)
        if abs(det) < 1e-12:
            continue
        inv = 1.0 / det
        tvec = _sub(point, a)
        u = _dot(tvec, pvec) * inv
        if u < 0.0 or u > 1.0:
            continue
        qvec = _cross(tvec, e1)
        v = _dot(_RAY, qvec) * inv
        if v < 0.0 or u + v > 1.0:
            continue
        t = _dot(e2, qvec) * inv
        if t > 1e-9:
            count += 1
    return count


def _inside(point, tris):
    return _crossings(point, tris) % 2 == 1


def depth(name_a, tris_a, name_b, tris_b):
    """Deepest vertex of either part inside the other, meters.

    The same measurement `genkart.driver_depth` makes -- parity for inside,
    distance to the other surface for depth, the rake-plane clip on
    `driver_torso` -- so the gate's waiver figures are checkable off the glb.
    """
    deepest = 0.0
    for own_name, own, other in (
        (name_a, tris_a, tris_b),
        (name_b, tris_b, tris_a),
    ):
        torso_side = "driver_torso" in (name_a, name_b)
        points = set()
        for tri in own:
            points.update(tri)
        for p in points:
            if torso_side:
                forward = rake_clearance_glb(p)
                if forward <= 0.0:
                    continue
            if not _inside(p, other):
                continue
            d = min(point_tri_dist2(p, a, b, c) for a, b, c in other) ** 0.5
            if torso_side and own_name != "driver_torso":
                d = min(d, forward)
            deepest = max(deepest, d)
    return deepest


def main(argv):
    pairs = []
    for argument in argv[1:]:
        if argument.startswith("--pair="):
            a, _, b = argument[len("--pair=") :].partition(":")
            if not b:
                raise SystemExit("driver_fit: --pair wants a:b")
            pairs.append((a, b))
        else:
            raise SystemExit("driver_fit: unknown argument %r" % argument)

    gltf, binchunk = load_glb(GLB)
    meshes = Meshes(gltf, binchunk)
    names = meshes.names()
    tolerance = joints.CONTACT_TOLERANCE

    if pairs:
        for a, b in pairs:
            gap = min_gap(meshes.tris(a), meshes.tris(b))
            deep = depth(a, meshes.tris(a), b, meshes.tris(b))
            print(
                "%-26s %-26s gap %7.2f mm  depth %7.2f mm"
                % (a, b, gap * 1000.0, deep * 1000.0)
            )
        return 0

    failed = 0
    print("declared contacts (tolerance %.1f mm):" % (tolerance * 1000.0))
    for pair, contact in sorted(joints.contacts(names).items()):
        gap = min_gap(meshes.tris(pair[0]), meshes.tris(pair[1]))
        ok = gap <= tolerance
        if not ok:
            failed += 1
        print(
            "  %-26s %-26s %-8s %7.2f mm  %s"
            % (pair[0], pair[1], contact.kind, gap * 1000.0, "ok" if ok else "FAIL")
        )

    print("gate 3 waivers (depth off the low-detail export):")
    for defect in joints.OPEN_DEFECTS:
        if defect.gate != "driver":
            continue
        expanded = sorted(
            (min(x, y), max(x, y))
            for x in fnmatch.filter(names, defect.a)
            for y in fnmatch.filter(names, defect.b)
            if x != y
        )
        for pair in expanded:
            # A glob waiver covers pairs that never overlap (brake_* is the
            # whole brake system); box-disjoint pairs are 0.00 by definition
            # and not worth the O(V x T) walk.
            if meshes.disjoint(pair[0], pair[1]):
                continue
            deep = depth(
                pair[0], meshes.tris(pair[0]), pair[1], meshes.tris(pair[1])
            )
            print(
                "  %-26s %-26s %7.2f mm  (waived %.2f, %s)"
                % (pair[0], pair[1], deep * 1000.0, defect.measured, defect.issue)
            )

    if failed:
        print("driver_fit: %d declared contact(s) out of tolerance" % failed)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
