#!/usr/bin/env python3
"""Author a design under `docs/circuits/` into a `track.json` the game loads.

    python3 tools/circuits/author_track.py
    python3 tools/circuits/author_track.py --stem=valdirone_nuova --out=data/tracks/

    --stem=NAME     which design under docs/circuits/ to author (default
                    valdirone_nuova)
    --out=DIR       where the .track.json goes (default data/tracks/)
    --report        print the derivation table and write nothing

`docs/circuits/valdirone_nuova.json` is a **design**: an argument about geometry,
with the corner-by-corner reasoning and the sourced checks attached. `track.json`
is a **track**: the same geometry in the schema three consumers read, with nothing
in it that a loader cannot act on. This script is the one place the first becomes
the second, and it is a script rather than a hand edit for three reasons.

  * The design's numbers are published rounded to two decimals and the loop
    therefore does not close. It is 6.2 mm out. That is a publication artifact and
    not a design error - the design's own `closure_note` says the unrounded solve
    closes at 5.9e-13 m - so the closure solve is re-run here at full precision
    rather than the gate being loosened to admit the rounding.
  * Tapers, banking transitions and vertical curves all have to become control
    points at derived stations. Doing that by hand for 60-odd points is how a
    width ends up 25 m from where its own design says it starts.
  * Re-authoring has to be free. A corner that moves in the design should move in
    the track by re-running one command, not by a session of arithmetic.

**The output is committed.** It is generated data under version control, in the
same sense `docs/circuits/valdirone_nuova_svg.json` is: a reviewer diffs it, the
game loads it on a fresh clone with no Python, and regenerating it is how it
changes.

## What this script does NOT take from the design's own derived files

`docs/circuits/valdirone_nuova_centerline.csv` is not read, and it must not be.
That file drifts from the exact walk of the segment list by up to **0.49 m** - its
generator advanced a quarter of a meter at every corner entry, which the circle
fits prove: the CSV's T1 arc centre is at (-56.9995, 87.7501) where the segment
list puts it at (-57, 88), one 0.25 m step short. Every radius, every turn angle
and the closure are right; the arc-length phase is not. The segment list is what
the design says a reader can reproduce from the file and nothing else, so the
segment list is what is authored from.

## Coordinate conversion, in one place

The design walks a plan frame with +X right and +Y forward, heading zero along +Y,
positive angles turning right. `track.json` is Godot's: Y up, -Z forward.

    x_godot = x_design      z_godot = -y_design      heading, turn sign unchanged

That is the whole conversion and this file is the only place it happens. ADR-0046:
the runtime is the consumer that must not have a transform bug, so the runtime
does not transform.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import sys

SCHEMA_VERSION = 1

#: The kart, for the clearances the schema checks against. FIA Karting Art. 8.1.1
#: caps overall width at 1,400 mm and this kart's rear track already is that; the
#: M2 gate measures the generated kart at 1.830 m long.
KART_WIDTH_M = 1.400
KART_LENGTH_M = 1.830

#: `src/core/circuit_reference.h`'s `ours::` namespace, duplicated here because
#: this script must run without a compiler. Any change belongs in that header
#: first; `tools/verify/circuit.sh` is what would catch a drift between the two,
#: because the loader validates against the header and the file was written here.
GRID_ROW_PITCH_M = 8.0
GRID_STAGGER_M = GRID_ROW_PITCH_M * 0.5
GRID_COLUMN_OFFSET_M = 3.0
GRID_FRONT_ROW_SETBACK_M = 4.0
BANKING_TRANSITION_M = 20.0
CHECKPOINT_MAX_SPACING_M = 100.0

#: How far apart two control points may be before one is inserted between them,
#: meters. Nothing needs it for correctness - the arcs are exact and the elevation
#: interpolant is exact - but a 165 m span with no interior point makes every
#: consumer's first act a subdivision, and it makes the file unreadable as a plot.
MAX_CONTROL_POINT_SPACING_M = 40.0

#: Rounding of the emitted numbers. Six decimals on a meter is a micron, which is
#: far below every tolerance in the schema and keeps the file diffable.
PLACES = 6


# --- the design's own geometry ---------------------------------------------


def segment_turn(segment: dict) -> float:
    """Signed turn of a segment, radians, positive to the right."""
    if segment["kind"] != "corner":
        return 0.0
    return math.radians(segment["angle_deg"])


def segment_length(segment: dict, lengths: list[float], index: int) -> float:
    return lengths[index]


def walk_frames(segments: list[dict], lengths: list[float]) -> list[tuple]:
    """Frame at the start of every segment: (distance, x, y, heading).

    Exact: an arc's endpoint is computed from its centre and its final heading
    rather than from an accumulation of steps, so the walk carries no
    discretisation error at all. That is the difference between this and the
    published centerline CSV.
    """
    frames = []
    x = y = heading = distance = 0.0
    for index, segment in enumerate(segments):
        frames.append((distance, x, y, heading))
        length = lengths[index]
        if segment["kind"] == "straight":
            x += math.sin(heading) * length
            y += math.cos(heading) * length
        else:
            turn = segment_turn(segment)
            radius = segment["radius_m"]
            hand = 1.0 if turn > 0.0 else -1.0
            centre_x = x + math.cos(heading) * hand * radius
            centre_y = y - math.sin(heading) * hand * radius
            heading += turn
            x = centre_x - math.cos(heading) * hand * radius
            y = centre_y + math.sin(heading) * hand * radius
        distance += length
    return frames


def close_the_loop(segments: list[dict]) -> tuple[list[float], dict]:
    """Re-solve the straight lengths so the walk closes exactly.

    The corner angles are fixed, so every straight's *direction* is fixed too and
    the endpoint of the walk is an exactly linear function of the straight
    lengths. One minimum-norm solve therefore closes the loop with no iteration
    and no tolerance:

        A dL = -r        A is 2 x n, its columns the straights' unit directions
        dL  = -A^T (A A^T)^-1 r

    Minimum-norm rather than "adjust one straight until it fits", because the
    design's nine straight runs came out of its own weighted least squares and
    the publication rounded each of them to two decimals. Spreading the residual
    back over all of them is undoing the rounding; putting it all on one straight
    would be moving a design number by nine times as much.

    Straights that are sub-splits of one physical run - Il Banco either side of
    the crest, La Discesa's three widths, La Rampa above and below its curve -
    share their run's correction in proportion to their length, so the crest stays
    at Il Banco's midpoint and the braking segment stays 50 m to the millimeter.
    """
    lengths = [
        s["length_m"] if s["kind"] == "straight" else s["arc_length_m"]
        for s in segments
    ]
    # Corner arc lengths are stated rounded too; recompute them from radius and
    # angle, which are exact by construction. T1's published 109.43 is 109.4271.
    for index, segment in enumerate(segments):
        if segment["kind"] == "corner":
            lengths[index] = abs(segment_turn(segment)) * segment["radius_m"]

    straights = [i for i, s in enumerate(segments) if s["kind"] == "straight"]

    # Group consecutive straights into runs, and treat the wrap: segment 23 and
    # segment 0 are both Rettifilo del Banco but they are separated by the start
    # line, which is a furniture position and not a geometric break, so they are
    # still two runs for the purposes of this solve. Nothing depends on it - the
    # solve is over individual straights and the grouping only sets how a run's
    # correction is shared.
    runs: list[list[int]] = []
    for index in straights:
        if runs and runs[-1][-1] == index - 1:
            runs[-1].append(index)
        else:
            runs.append([index])

    def heading_at(index: int, lengths: list[float]) -> float:
        heading = 0.0
        for i in range(index):
            heading += segment_turn(segments[i])
        return heading

    directions = [
        (math.sin(heading_at(run[0], lengths)), math.cos(heading_at(run[0], lengths)))
        for run in runs
    ]

    frames = walk_frames(segments, lengths)
    end_x = frames[-1][1]
    end_y = frames[-1][2]
    # Close the last segment to get the true terminus.
    last = len(segments) - 1
    if segments[last]["kind"] == "straight":
        heading = frames[last][3]
        end_x += math.sin(heading) * lengths[last]
        end_y += math.cos(heading) * lengths[last]
    else:
        turn = segment_turn(segments[last])
        radius = segments[last]["radius_m"]
        hand = 1.0 if turn > 0.0 else -1.0
        heading = frames[last][3]
        centre_x = end_x + math.cos(heading) * hand * radius
        centre_y = end_y - math.sin(heading) * hand * radius
        heading += turn
        end_x = centre_x - math.cos(heading) * hand * radius
        end_y = centre_y + math.sin(heading) * hand * radius

    residual = (end_x, end_y)

    # dL = -A^T (A A^T)^-1 r, with A A^T a 2x2 built by hand. No numpy: this
    # script runs wherever python3 does and a 2x2 inverse is four lines.
    a11 = sum(d[0] * d[0] for d in directions)
    a12 = sum(d[0] * d[1] for d in directions)
    a22 = sum(d[1] * d[1] for d in directions)
    determinant = a11 * a22 - a12 * a12
    if abs(determinant) < 1e-12:
        raise SystemExit("author_track: the straights are all parallel; the loop cannot be closed")
    inv11 = a22 / determinant
    inv12 = -a12 / determinant
    inv22 = a11 / determinant
    lam = (
        inv11 * residual[0] + inv12 * residual[1],
        inv12 * residual[0] + inv22 * residual[1],
    )

    corrections = {}
    for run, direction in zip(runs, directions):
        delta = -(direction[0] * lam[0] + direction[1] * lam[1])
        run_length = sum(lengths[i] for i in run)
        for index in run:
            share = delta * lengths[index] / run_length
            lengths[index] += share
            corrections[index] = share

    frames = walk_frames(segments, lengths)
    report = {
        "closure_before_m": math.hypot(*residual),
        "corrections_m": corrections,
        "largest_correction_m": max(abs(v) for v in corrections.values()),
    }
    return lengths, report


class Geometry:
    """The closed centerline: exact position, heading and curvature at any station."""

    def __init__(self, segments: list[dict], lengths: list[float]):
        self.segments = segments
        self.lengths = lengths
        self.frames = walk_frames(segments, lengths)
        self.total = sum(lengths)
        self.starts = [f[0] for f in self.frames]

    def segment_at(self, distance: float) -> int:
        """The segment whose span *leaves* this station.

        Ties go forward: a station exactly on a boundary belongs to the segment
        beginning there, because a control point describes the span leaving it.
        """
        found = 0
        for index, start in enumerate(self.starts):
            if start <= distance + 1e-9:
                found = index
        return min(found, len(self.segments) - 1)

    def curvature(self, distance: float) -> float:
        index = self.segment_at(distance)
        segment = self.segments[index]
        if segment["kind"] != "corner":
            return 0.0
        turn = segment_turn(segment)
        return (1.0 if turn > 0.0 else -1.0) / segment["radius_m"]

    def frame(self, distance: float) -> tuple[float, float, float]:
        """(x, y, heading) in the *design's* plan frame."""
        index = self.segment_at(distance)
        start, x, y, heading = self.frames[index]
        travel = distance - start
        segment = self.segments[index]
        if segment["kind"] == "straight":
            return x + math.sin(heading) * travel, y + math.cos(heading) * travel, heading
        turn = segment_turn(segment)
        radius = segment["radius_m"]
        hand = 1.0 if turn > 0.0 else -1.0
        centre_x = x + math.cos(heading) * hand * radius
        centre_y = y - math.sin(heading) * hand * radius
        swept = hand * travel / radius
        heading += swept
        return centre_x - math.cos(heading) * hand * radius, centre_y + math.sin(heading) * hand * radius, heading


# --- the profiles ----------------------------------------------------------


def piecewise(points: list[tuple[float, float]], distance: float) -> float:
    """Linear interpolation over a sorted list of (station, value) breakpoints."""
    if distance <= points[0][0]:
        return points[0][1]
    for (d0, v0), (d1, v1) in zip(points, points[1:]):
        if distance <= d1 + 1e-9:
            if d1 - d0 <= 1e-12:
                return v1
            t = (distance - d0) / (d1 - d0)
            return v0 + (v1 - v0) * t
    return points[-1][1]


def width_breakpoints(segments: list[dict], geometry: Geometry) -> list[tuple[float, float]]:
    """A taper is two control points, not a note string.

    `width_taper_in_m` on a segment means "over the first N meters of this
    segment, the road goes from the previous segment's width to this one's". That
    is exactly a pair of breakpoints, and the schema's linear-in-arc-length rule
    then reproduces the taper in both consumers with no shared code.
    """
    points: list[tuple[float, float]] = []
    for index, segment in enumerate(segments):
        start = geometry.starts[index]
        taper = float(segment.get("width_taper_in_m", 0.0) or 0.0)
        width = float(segment["width_m"])
        previous = float(segments[index - 1]["width_m"])
        if taper > 0.0:
            if abs(width - previous) > 1e-9:
                points.append((start, previous))
                points.append((start + taper, width))
            else:
                points.append((start, width))
        else:
            if index > 0 and abs(width - previous) > 1e-9:
                raise SystemExit(
                    "author_track: segment %d changes width %.2f -> %.2f with no taper"
                    % (index, previous, width)
                )
            points.append((start, width))
    points.append((geometry.total, points[0][1]))
    return sorted(points, key=lambda p: p[0])


def cross_section_breakpoints(
    segments: list[dict], geometry: Geometry
) -> tuple[list[tuple[float, float]], list[tuple[float, float]]]:
    """Crown and bank, with a stated transition length at every change.

    A straight carries drainage crown and no bank; a corner carries bank toward
    its own inside and no crown. Both are ramped over
    `BANKING_TRANSITION_M`, clamped to half of each adjacent span so a short arc
    between two bankings develops over what it has rather than overlapping its
    neighbour's ramp.

    The sign convention is the schema's: bank positive means the road falls to the
    *right* of travel, so a right-hander banks positive and a left-hander
    negative. A bank whose sign opposes its own turn is adverse camber, which the
    regulation calls "not generally acceptable" and the loader rejects.
    """
    crown_target: list[float] = []
    bank_target: list[float] = []
    for segment in segments:
        if segment["kind"] == "straight":
            crown_target.append(float(segment["camber_pct"]))
            bank_target.append(0.0)
        else:
            crown_target.append(0.0)
            hand = 1.0 if segment_turn(segment) > 0.0 else -1.0
            bank_target.append(hand * float(segment["banking_pct"]))

    crown: list[tuple[float, float]] = []
    bank: list[tuple[float, float]] = []
    count = len(segments)
    for index in range(count):
        start = geometry.starts[index]
        previous = (index - 1) % count
        ramp = min(
            BANKING_TRANSITION_M,
            0.5 * geometry.lengths[previous],
            0.5 * geometry.lengths[index],
        )
        changed = (
            abs(crown_target[index] - crown_target[previous]) > 1e-9
            or abs(bank_target[index] - bank_target[previous]) > 1e-9
        )
        if index == 0:
            # The start line sits inside a run of constant cross-section; the wrap
            # is handled by the closing breakpoint below.
            crown.append((0.0, crown_target[0]))
            bank.append((0.0, bank_target[0]))
            continue
        if changed and ramp > 1e-6:
            crown.append((start - ramp, crown_target[previous]))
            bank.append((start - ramp, bank_target[previous]))
            crown.append((start + ramp, crown_target[index]))
            bank.append((start + ramp, bank_target[index]))
        else:
            crown.append((start, crown_target[index]))
            bank.append((start, bank_target[index]))

    crown.append((geometry.total, crown_target[0]))
    bank.append((geometry.total, bank_target[0]))
    return sorted(crown, key=lambda p: p[0]), sorted(bank, key=lambda p: p[0])


class Elevation:
    """The longitudinal profile: four constant-grade bands, three parabolic curves.

    The grade of the level band is **solved, not chosen**. The two steep bands are
    fixed at -4.60% and +4.60% by the design, so the remaining band's grade is
    whatever closes the loop in height, and the design's published 0.787% is that
    number rounded. Solving it here rather than copying it is what keeps the
    elevation closing to a micron after the plan closure has moved every band
    boundary by a millimeter or two.
    """

    def __init__(self, design: dict, geometry: Geometry, segments: list[dict]):
        curves = design["elevation"]["vertical_curves"]
        # The design states that every grade break sits exactly on a segment
        # boundary. Rather than trust the published station, find which boundary
        # each break is nearest and take the boundary - so the profile follows the
        # closure solve instead of drifting off it.
        self.break_segments = []
        for curve in curves:
            wanted = curve["at_distance_m"]
            index = min(
                range(len(geometry.starts)),
                key=lambda i: abs(geometry.starts[i] - wanted),
            )
            if abs(geometry.starts[index] - wanted) > 0.05:
                raise SystemExit(
                    "author_track: vertical curve at %.2f is not on a segment boundary"
                    % wanted
                )
            self.break_segments.append(index)

        self.breaks = [geometry.starts[i] for i in self.break_segments]
        self.total = geometry.total

        steep_down = float(curves[0]["delta_grade_pct"])  # unused; kept for shape
        # Grades of the four bands. Bands 2 and 3 are the design's fixed +/-4.60%,
        # read off the segments rather than off the prose.
        self.g_descent = float(segments[self.break_segments[0]]["grade_pct"]) / 100.0
        self.g_climb = float(segments[self.break_segments[1]]["grade_pct"]) / 100.0

        level_length = self.breaks[0] + (self.total - self.breaks[2])
        descent_length = self.breaks[1] - self.breaks[0]
        climb_length = self.breaks[2] - self.breaks[1]
        self.g_level = -(self.g_descent * descent_length + self.g_climb * climb_length) / level_length

        self.grades = [self.g_level, self.g_descent, self.g_climb, self.g_level]

        # Curve lengths from the regulation's own relation, L = R |dg|, rather
        # than from the published rounded length.
        self.curves = []
        for index, curve in enumerate(curves):
            g_in = self.grades[index]
            g_out = self.grades[index + 1]
            radius = float(curve["chosen_radius_m"])
            length = radius * abs(g_out - g_in)
            self.curves.append(
                {
                    "at_m": self.breaks[index],
                    "profile": curve["profile"],
                    "K": float(curve["K"]),
                    "radius_m": radius,
                    "length_m": length,
                    "grade_in_pct": g_in * 100.0,
                    "grade_out_pct": g_out * 100.0,
                    "speed_forward_kmh": float(curve["speed_forward_kmh"]),
                    "speed_reverse_kmh": float(curve["speed_reverse_kmh"]),
                    "vertical_acceleration_g": float(curve["vertical_acceleration_g"]),
                    "note": curve["note"],
                }
            )
        self._check_curves()

    def _check_curves(self) -> None:
        for index, curve in enumerate(self.curves):
            half = curve["length_m"] * 0.5
            start = curve["at_m"] - half
            end = curve["at_m"] + half
            lower = 0.0 if index == 0 else self.breaks[index - 1]
            upper = self.total if index == len(self.curves) - 1 else self.breaks[index + 1]
            if start < lower - 1e-6 or end > upper + 1e-6:
                raise SystemExit(
                    "author_track: vertical curve %d (%.2f m long) does not fit its bands"
                    % (index, curve["length_m"])
                )

    def tangent(self, distance: float) -> float:
        z = 0.0
        previous = 0.0
        for index, station in enumerate(self.breaks):
            if distance <= station:
                return z + self.grades[index] * (distance - previous)
            z += self.grades[index] * (station - previous)
            previous = station
        return z + self.grades[-1] * (distance - previous)

    def at(self, distance: float) -> tuple[float, float]:
        """(elevation, grade) at a station. Grade is a fraction, not a percent."""
        for index, curve in enumerate(self.curves):
            half = curve["length_m"] * 0.5
            start = curve["at_m"] - half
            end = curve["at_m"] + half
            if start - 1e-9 <= distance <= end + 1e-9:
                g_in = self.grades[index]
                g_out = self.grades[index + 1]
                length = curve["length_m"]
                t = distance - start
                z = self.tangent(start) + g_in * t + (g_out - g_in) * t * t / (2.0 * length)
                grade = g_in + (g_out - g_in) * t / length
                return z, grade
        band = 0
        for index, station in enumerate(self.breaks):
            if distance > station:
                band = index + 1
        return self.tangent(distance), self.grades[band]

    def stations(self) -> list[float]:
        """Where a control point is needed for the profile to be exact."""
        found = []
        for curve in self.curves:
            half = curve["length_m"] * 0.5
            found.append(curve["at_m"] - half)
            found.append(curve["at_m"] + half)
        return found


# --- the surfaces ----------------------------------------------------------
#
# The design specifies its kerbs and its run-off in prose, corner by corner, with
# the reason attached. Prose is the right medium for the reason and the wrong one
# for a generator, so the numbers are transcribed here once, each beside the
# sentence it came from. A transcription table is honest about being one; parsing
# the prose would not be.


def curb_spans(corners: dict, geometry: Geometry) -> list[dict]:
    def arc(name: str, from_fraction: float, to_fraction: float) -> tuple[float, float]:
        corner = corners[name]
        span = corner["to_m"] - corner["from_m"]
        return corner["from_m"] + span * from_fraction, corner["from_m"] + span * to_fraction

    spans: list[dict] = []

    # T1: "Inside kerb over the middle 60 deg of the arc, red/white 1 m
    # alternation, 30 mm. No exit kerb."  110 deg of arc, so the middle 60 runs
    # from 25/110 to 85/110 of it.
    a, b = arc("T1 - Ronda", 25.0 / 110.0, 85.0 / 110.0)
    spans.append(dict(from_m=a, to_m=b, side="left", type="curb", width_m=1.0,
                      height_m=0.030, profile="rippled",
                      note="T1 Ronda, inside kerb over the middle 60 deg of the arc"))

    # T2: "30 mm VERTICAL-faced kerb, 24 m of it on the inside, and this is the
    # reference kerb of the circuit ... Flat 20 mm exit strip on the outside so
    # that running wide on the descent is a track-limits call and not a launch."
    corner = corners["T2 - Lama"]
    middle = 0.5 * (corner["from_m"] + corner["to_m"])
    spans.append(dict(from_m=middle - 12.0, to_m=middle + 12.0, side="right", type="curb",
                      width_m=1.0, height_m=0.030, profile="vertical",
                      note="T2 Lama, the reference kerb - the only one on the circuit struck above 120 km/h"))
    spans.append(dict(from_m=corner["to_m"] - 20.0, to_m=corner["to_m"] + 15.0, side="left",
                      type="curb", width_m=1.0, height_m=0.020, profile="flat",
                      note="T2 Lama, flat exit strip on the outside - running wide is a track-limits call, not a launch"))

    # T3: "Full inside kerb through both 90 deg halves, 30 mm rippled, plus a flat
    # exit kerb on the left for the first 15 m of the climb. Flat rather than
    # rippled because reversed that same edge is an entry kerb taken at 52 km/h."
    corner = corners["T3 - Il Pozzo"]
    spans.append(dict(from_m=corner["from_m"], to_m=corner["to_m"], side="left", type="curb",
                      width_m=1.0, height_m=0.030, profile="rippled",
                      note="T3 Il Pozzo, full inside kerb through both 90 deg halves"))
    spans.append(dict(from_m=corner["to_m"], to_m=corner["to_m"] + 15.0, side="left", type="curb",
                      width_m=1.0, height_m=0.030, profile="flat",
                      note="T3 Il Pozzo, flat exit kerb up the first 15 m of La Rampa - reversed it is an entry kerb at 52 km/h"))

    # T4: "Inside kerb, 30 mm rippled, whole arc. No exit kerb: running wide here
    # loses Vigna."
    corner = corners["T4 - Il Ciglione"]
    spans.append(dict(from_m=corner["from_m"], to_m=corner["to_m"], side="right", type="curb",
                      width_m=1.0, height_m=0.030, profile="rippled",
                      note="T4 Il Ciglione, inside kerb over the whole arc"))

    # T5: "Inside kerb through the 22 m apex arc only ... T5a's inside carries no
    # kerb: a kerb there is an invitation to straighten the compound."
    corner = corners["T5 - Vigna"]
    spans.append(dict(from_m=corner["apex_arc_from_m"], to_m=corner["to_m"], side="left", type="curb",
                      width_m=1.0, height_m=0.030, profile="rippled",
                      note="T5 Vigna, apex arc only - a kerb on T5a's inside would straighten the compound"))

    # T6 / T7: "25 mm at the apex, identical profile to T7 - the test is a
    # comparison."  The apex is the middle half of the arc; the design gives the
    # height and the requirement that the two match, and nothing else, so the
    # extent is this tool's and it is identical for both by construction.
    for name, side in (("T6 - Forbice A", "left"), ("T7 - Forbice B", "right")):
        a, b = arc(name, 0.25, 0.75)
        spans.append(dict(from_m=a, to_m=b, side=side, type="curb", width_m=1.0,
                          height_m=0.025, profile="rippled",
                          note="%s, apex kerb - identical profile to its twin, because the test is a comparison" % name))

    # T8: "Inside kerb through T8a and the first 40 deg of T8b, 30 mm rippled.
    # ... Reversed, the T8a inside kerb becomes an exit kerb struck at 111.9 km/h
    # with the corner tightening, and it is respecified 25 mm flat for that
    # layout."  One piece of concrete cannot be two heights; T8a is built at the
    # gentler of the two, which is the rule the design already applies to run-off.
    # ADR-0049.
    corner = corners["T8 - Uscita"]
    spans.append(dict(from_m=corner["from_m"], to_m=corner["apex_arc_from_m"], side="left", type="curb",
                      width_m=1.0, height_m=0.025, profile="flat",
                      note="T8a Uscita, built at the reverse layout's 25 mm flat rather than the forward layout's 30 mm rippled - one kerb, gentler case wins. ADR-0049"))
    span = corner["to_m"] - corner["apex_arc_from_m"]
    spans.append(dict(from_m=corner["apex_arc_from_m"],
                      to_m=corner["apex_arc_from_m"] + span * 40.0 / 97.0,
                      side="left", type="curb", width_m=1.0, height_m=0.030, profile="rippled",
                      note="T8b Uscita, the first 40 deg of the opening arc"))

    return spans


#: Run-off, transcribed from each corner's `runoff` sentence in the design.
#:
#: `sized_for` records which direction's approach speed drove the build, because
#: two corners are sized by the reverse layout and that is the design's concrete
#: case for run-off being a per-layout figure rather than shared furniture.
RUNOFF = {
    "T1 - Ronda": dict(side="right", apron_m=10.0, outfield_m=24.0, outfield="grass",
                       barrier="continuous_conveyor", sized_for="reverse", approach_kmh=138.0,
                       note="Departure angle off a 76.56 m line is about 11 deg, so Part I section 8's small-impact-angle case: a smooth continuous barrier with a conveyor face."),
    "T2 - Lama": dict(side="left", apron_m=12.0, outfield_m=3.0, outfield="gravel",
                      barrier="tire_type_c", sized_for="forward", approach_kmh=142.5,
                      note="Gravel at 5/15 granulometry and 300 mm depth, then a Type C tire barrier with 500 mm behind it."),
    "T3 - Il Pozzo": dict(side="right", apron_m=14.0, outfield_m=3.0, outfield="gravel",
                          barrier="foam_type_b", sized_for="forward", approach_kmh=140.8,
                          note="The honest limit, stated by the design: a total brake failure arrives at 140.8 km/h and needs about 105 m of gravel at 0.75 g. That is not buildable and is not built; it is handed to the barrier."),
    "T4 - Il Ciglione": dict(side="left", apron_m=6.0, outfield_m=20.0, outfield="gravel",
                             barrier="tire_type_c", sized_for="forward", approach_kmh=125.4,
                             note="Free from the landform: the axis points into the cut face the climb was excavated from. Apron graded at art 7.5's maximum 10% up-slope, then rising gravel, then a tire barrier against the rock."),
    "T5 - Vigna": dict(side="right", apron_m=10.0, outfield_m=18.0, outfield="grass",
                       barrier="tire_type_c", sized_for="reverse", approach_kmh=123.6,
                       note="On each of the two tangent axes; the reverse approach is the binding case."),
    "T6 - Forbice A": dict(side="both", apron_m=10.0, outfield_m=18.0, outfield="grass",
                           barrier="tire_type_c", sized_for="forward", approach_kmh=121.5,
                           note="Asphalt on BOTH sides - an esse throws a kart off in either direction, and this is the one place on the circuit where the run-off has to be symmetric. The design gives the apron only; the outfield width is this tool's, matched to T5."),
    "T7 - Forbice B": dict(side="both", apron_m=10.0, outfield_m=18.0, outfield="grass",
                           barrier="tire_type_c", sized_for="reverse", approach_kmh=128.0,
                           note="The other half of the matched pair, built identically so the comparison is between the corners and not between their run-off. The design gives no dimensions for this one beyond 'built for reverse'; they are T6's."),
    "T8 - Uscita": dict(side="right", apron_m=14.0, outfield_m=26.0, outfield="grass",
                        barrier="tire_type_c", sized_for="reverse", approach_kmh=142.2,
                        note="Forward approach 124.3 km/h, reverse 142.2 - 1.31x the kinetic energy - so the reverse figure is the one built, against the 10 m plus 16 m the forward case alone would need. The concrete case for run-off being a per-layout field."),
}


# --- assembling the file ---------------------------------------------------


def build_corners(design: dict, geometry: Geometry) -> dict:
    corners = {}
    for corner in design["corners"]:
        indices = corner["segment_indices"]
        first = indices[0]
        last = indices[-1]
        from_m = geometry.starts[first]
        to_m = geometry.starts[last] + geometry.lengths[last]
        # The apex arc is the tightest one, and on a compound it is where the kerb
        # and the seed's apex go. On a single-arc corner it is the corner.
        tightest = min(indices, key=lambda i: design["segments"][i]["radius_m"])
        corners[corner["name"]] = {
            "name": corner["name"],
            "from_m": from_m,
            "to_m": to_m,
            "apex_arc_from_m": geometry.starts[tightest],
            "apex_arc_to_m": geometry.starts[tightest] + geometry.lengths[tightest],
            "hand": corner["hand"],
            "direction_change_deg": float(corner["angle_deg"]),
            "min_radius_m": float(corner["radius_m"]),
            "width_m": float(corner["width_m"]),
            "line_radius_m": float(corner["line_radius_m"]),
            "defender_line_radius_m": float(corner["defender_line_radius_m"]),
            "grip_ceiling_kmh": float(corner["grip_ceiling_kmh"]),
            "lock_ceiling_kmh": float(corner["lock_ceiling_kmh"]),
            "segment_indices": indices,
            "apex_kmh": float(corner["apex_kmh"]),
            "reverse_apex_kmh": float(corner["reverse"]["apex_kmh"]),
            "provokes": corner["provokes"].split(".")[0] + ".",
        }
    return corners


def control_point_stations(
    geometry: Geometry,
    widths: list[tuple[float, float]],
    crowns: list[tuple[float, float]],
    banks: list[tuple[float, float]],
    elevation: Elevation,
) -> list[float]:
    stations = set()
    for start in geometry.starts:
        stations.add(round(start, 9))
    for points in (widths, crowns, banks):
        for station, _value in points:
            if 0.0 <= station < geometry.total:
                stations.add(round(station, 9))
    for station in elevation.stations():
        if 0.0 <= station < geometry.total:
            stations.add(round(station, 9))
    stations.add(0.0)

    # Coincident stations, and why this is not a rounding nicety. La Discesa's
    # widening segment is 25.0 m long and tapers over 25.0 m, so its taper ends
    # exactly where the hairpin begins, and the two stations differ by 0.1 mm of
    # accumulated float. That produces a span of length 0.0001 m, which is a
    # division by nearly zero in every consumer and a real landmine in the
    # projection. Merged at a millimeter - below every tolerance in the schema -
    # keeping the earlier station, which is the segment boundary.
    ordered = []
    for station in sorted(stations):
        if ordered and station - ordered[-1] < 1e-3:
            continue
        ordered.append(station)

    # Fill the long gaps so the file plots as a road rather than as a chord.
    filled: list[float] = []
    for a, b in zip(ordered, ordered[1:] + [geometry.total]):
        filled.append(a)
        gap = b - a
        if gap > MAX_CONTROL_POINT_SPACING_M:
            steps = int(math.ceil(gap / MAX_CONTROL_POINT_SPACING_M))
            for step in range(1, steps):
                filled.append(a + gap * step / steps)
    return [s for s in filled if s < geometry.total - 1e-9]


def build_spline(design, geometry, widths, crowns, banks, elevation) -> list[dict]:
    labels = {round(geometry.starts[i], 6): design["segments"][i]["label"]
              for i in range(len(design["segments"]))}
    points = []
    for station in control_point_stations(geometry, widths, crowns, banks, elevation):
        x, y, heading = geometry.frame(station)
        z, grade = elevation.at(station)
        point = {
            "distance_m": round(station, PLACES),
            # The one coordinate conversion in the project: design +Y forward
            # becomes Godot -Z forward.
            "position": [round(x, PLACES), round(-y, PLACES)],
            "heading_deg": round(math.degrees(heading), PLACES),
            "curvature_1pm": round(geometry.curvature(station), 9),
            "width_m": round(piecewise(widths, station), PLACES),
            "crown_pct": round(piecewise(crowns, station), PLACES),
            "bank_pct": round(piecewise(banks, station), PLACES),
            "elevation_m": round(z, PLACES),
            "grade_pct": round(grade * 100.0, PLACES),
            "segment": geometry.segment_at(station),
        }
        label = labels.get(round(station, 6))
        if label:
            point["note"] = label
        points.append(point)
    return points


def build_layout(name, direction, design, geometry, corners, elevation, sector_marks) -> dict:
    total = geometry.total
    forward = direction == "forward"

    def station(forward_distance: float) -> float:
        if forward:
            return forward_distance % total
        return (total - forward_distance) % total

    checkpoint_count = int(math.ceil(total / CHECKPOINT_MAX_SPACING_M))
    spacing = total / checkpoint_count
    checkpoints = [round(i * spacing, PLACES) for i in range(checkpoint_count)]

    # Pole goes to the inside of the first corner, because that is what the inside
    # line is worth: 13.0 km/h of corner ceiling at T1 forward.
    first_corner_hand = "left" if forward else "right"
    order = sorted(corners.values(), key=lambda c: station(c["from_m"]))
    if order:
        hand = order[0]["hand"]
        first_corner_hand = hand if forward else ("right" if hand == "left" else "left")

    slots = []
    for index in range(8):
        setback = GRID_FRONT_ROW_SETBACK_M + index * GRID_STAGGER_M
        pole_side = -1.0 if first_corner_hand == "left" else 1.0
        side = pole_side if index % 2 == 0 else -pole_side
        slots.append(
            {
                "position": index + 1,
                "distance_m": round((total - setback) % total, PLACES),
                "lateral_m": round(side * GRID_COLUMN_OFFSET_M, PLACES),
            }
        )

    seed = []
    for corner in sorted(corners.values(), key=lambda c: station(c["from_m"])):
        half = (corner["width_m"] - KART_WIDTH_M) * 0.5
        hand = corner["hand"]
        if not forward:
            hand = "right" if hand == "left" else "left"
        inside = -1.0 if hand == "left" else 1.0
        apex = 0.5 * (corner["apex_arc_from_m"] + corner["apex_arc_to_m"])
        entry, exit_ = (corner["from_m"], corner["to_m"]) if forward else (corner["to_m"], corner["from_m"])
        seed.append({"at_m": round(station(entry), PLACES), "lateral_m": round(-inside * half, PLACES)})
        seed.append({"at_m": round(station(apex), PLACES), "lateral_m": round(inside * half, PLACES)})
        seed.append({"at_m": round(station(exit_), PLACES), "lateral_m": round(-inside * half, PLACES)})
    seed.sort(key=lambda p: p["at_m"])

    speeds = []
    for corner in sorted(corners.values(), key=lambda c: station(c["from_m"])):
        speeds.append(
            {
                "corner": corner["name"],
                "at_m": round(station(corner["apex_arc_from_m"]), PLACES),
                "apex_kmh": corner["apex_kmh"] if forward else corner["reverse_apex_kmh"],
            }
        )

    curve_speeds = [
        c["speed_forward_kmh"] if forward else c["speed_reverse_kmh"]
        for c in elevation.curves
    ]

    return {
        "name": name,
        "direction": direction,
        "sector_marks_m": [round(m, PLACES) for m in sector_marks],
        "checkpoints_m": checkpoints,
        "grid": {
            "slots": 8,
            "pole_side": "left" if first_corner_hand == "left" else "right",
            "pole_note": "Pole takes the inside of the first corner: %s. The design measures that at 13.0 km/h of corner ceiling forward." % first_corner_hand,
            "positions": slots,
        },
        "pit_entry_m": 1305.0,
        "pit_exit_m": 62.0,
        "pit_note": "Stations only. A deceleration lane at 20 deg to the direction of travel is a 160 deg merge driven the other way, over Part I art 7.4's 30 deg cap, so each layout needs its own stubs and no pit-lane asphalt is generated yet. Issue #181.",
        "racing_line_seed": seed,
        "corner_speeds_kmh": speeds,
        "vertical_curve_speeds_kmh": curve_speeds,
        "estimated_lap_time_s": (
            design["measured"]["estimated_lap_time_s"] if forward
            else design["measured"]["estimated_reverse_lap_time_s"]
        ),
    }


def author(design: dict, stem: str) -> tuple[dict, dict]:
    segments = design["segments"]
    lengths, closure = close_the_loop(segments)
    geometry = Geometry(segments, lengths)
    elevation = Elevation(design, geometry, segments)
    widths = width_breakpoints(segments, geometry)
    crowns, banks = cross_section_breakpoints(segments, geometry)
    corners = build_corners(design, geometry)

    spline = build_spline(design, geometry, widths, crowns, banks, elevation)

    corner_list = []
    for name in sorted(corners, key=lambda n: corners[n]["from_m"]):
        corner = dict(corners[name])
        runoff = dict(RUNOFF[name])
        corner["runoff"] = runoff
        corner["line_radius_construction"] = (
            "rho = R + h(1+cos(theta/2))/(1-cos(theta/2)), h = (W - 1.400)/2, "
            "R the tightest arc and theta the whole direction change"
        )
        corner.pop("segment_indices", None)
        corner_list.append(
            {
                key: (round(value, PLACES) if isinstance(value, float) else value)
                for key, value in corner.items()
            }
        )

    surfaces = curb_spans(corners, geometry)
    for span in surfaces:
        for key in ("from_m", "to_m"):
            span[key] = round(span[key] % geometry.total, PLACES)

    forward_marks = [524.0, 902.0]
    reverse_marks = [473.0, 862.0]

    track = {
        "schema_version": SCHEMA_VERSION,
        "meta": {
            "name": design["working_name"],
            "length_m": round(geometry.total, PLACES),
            "grade": 1,
            "net_turn_deg": round(sum(math.degrees(segment_turn(s)) for s in segments), PLACES),
            "designed_from": "docs/circuits/%s.json" % stem,
            "authored_by": "tools/circuits/author_track.py",
            "note": "Generated. Edit the design and re-run; a hand edit here will fail the position checksum in src/core/track.h.",
        },
        "spline": spline,
        "corners": corner_list,
        "elevation": {
            "vertical_curves": [
                {key: (round(value, PLACES) if isinstance(value, float) else value)
                 for key, value in curve.items()}
                for curve in elevation.curves
            ],
            "note": "K does not swap when the layout reverses: d2z/ds2 is invariant under s -> L-s, so a crest is a crest driven either way and only the speed changes.",
        },
        "surfaces": surfaces,
        "furniture": {
            "start_line": {"distance_m": 0.0, "width_m": 0.30},
            "lights": {
                "distance_m": 10.0,
                "height_m": 3.0,
                "span_m": 12.0,
                "note": "Design's figures, quoted from Part I art 7.7: 10-15 m ahead of the front row, 2.5-3.5 m above the track.",
            },
        },
        "layouts": [
            build_layout("forward", "forward", design, geometry, corners, elevation, forward_marks),
            build_layout("reverse", "reverse", design, geometry, corners, elevation, reverse_marks),
        ],
    }

    report = {
        "closure_before_m": closure["closure_before_m"],
        "largest_length_correction_m": closure["largest_correction_m"],
        "length_m": geometry.total,
        "level_grade_pct": elevation.g_level * 100.0,
        "elevation_closure_m": elevation.at(geometry.total - 1e-9)[0],
        "control_points": len(spline),
        "curve_lengths_m": [c["length_m"] for c in elevation.curves],
    }
    return track, report


def build_self_intersecting() -> dict:
    """The negative control: a closed loop that crosses itself.

    Deliberately broken, committed, and run by `tools/verify/circuit.sh`, which
    fails if it *loads*. Same principle as `input_push_probe.gd --break`: a
    validator with no negative control is a validator nobody has proven can say no.

    **Exactly one thing is wrong with it**, and that is the whole design of the
    file. An earlier version of this control was a figure-eight that also failed
    to close, sat on a 260 m straight and put its grid inside a corner, and the
    validator dutifully reported nine problems - which proves that *something* is
    wrong and not that the self-intersection rule fires. So this one closes to
    3e-13 m, turns +360 degrees like a real loop, is 1,105 m long with no straight
    over 150 m, has legal camber and banking, has its grid on a straight and its
    checkpoints inside the anti-cut spacing. The only rule it breaks is the one it
    exists to break.

    The shape is +270, -90, +270, -90: net +360, so it is a genuine loop rather
    than a figure-eight, and the two 270-degree corners throw the road back across
    its own path. The four straight lengths are then closed by the same minimum-norm
    solve the real circuit uses.
    """
    radius = 45.0
    # (kind, angle_deg) with the straights' base lengths beside them. The start line
    # sits 75 m into the long straight rather than at its beginning, so the
    # last-corner-to-line clearance is a legal 75 m instead of zero - the control
    # must not fail a rule it is not testing.
    pieces = [
        ("straight", 75.0),
        ("corner", 270.0),
        ("straight", 120.0),
        ("corner", -90.0),
        ("straight", 150.0),
        ("corner", 270.0),
        ("straight", 120.0),
        ("corner", -90.0),
        ("straight", 75.0),
    ]
    lengths = [
        value if kind == "straight" else abs(math.radians(value)) * radius
        for kind, value in pieces
    ]

    def heading_before(index: int) -> float:
        heading = 0.0
        for kind, value in pieces[:index]:
            if kind == "corner":
                heading += math.radians(value)
        return heading

    def terminus(lengths: list[float]):
        x = y = heading = 0.0
        frames = []
        distance = 0.0
        for index, (kind, value) in enumerate(pieces):
            frames.append((distance, x, y, heading))
            length = lengths[index]
            if kind == "straight":
                x += math.sin(heading) * length
                y += math.cos(heading) * length
            else:
                turn = math.radians(value)
                hand = 1.0 if turn > 0.0 else -1.0
                centre_x = x + math.cos(heading) * hand * radius
                centre_y = y - math.sin(heading) * hand * radius
                heading += turn
                x = centre_x - math.cos(heading) * hand * radius
                y = centre_y + math.sin(heading) * hand * radius
            distance += length
        return x, y, heading, distance, frames

    straights = [i for i, (kind, _v) in enumerate(pieces) if kind == "straight"]
    directions = [
        (math.sin(heading_before(i)), math.cos(heading_before(i))) for i in straights
    ]
    end_x, end_y, _heading, _total, _frames = terminus(lengths)
    a11 = sum(d[0] * d[0] for d in directions)
    a12 = sum(d[0] * d[1] for d in directions)
    a22 = sum(d[1] * d[1] for d in directions)
    determinant = a11 * a22 - a12 * a12
    inv11 = a22 / determinant
    inv12 = -a12 / determinant
    inv22 = a11 / determinant
    lam = (inv11 * end_x + inv12 * end_y, inv12 * end_x + inv22 * end_y)
    for index, direction in zip(straights, directions):
        lengths[index] -= direction[0] * lam[0] + direction[1] * lam[1]

    end_x, end_y, end_heading, total, frames = terminus(lengths)

    # Control points: the piece boundaries, plus subdivision so the O(n^2)
    # separation scan has samples to find the crossing with. The real circuit is
    # subdivided the same way and for the same reason.
    spline = []
    corner_spans = []
    for index, (kind, value) in enumerate(pieces):
        distance, x, y, heading = frames[index]
        length = lengths[index]
        curvature = 0.0 if kind == "straight" else (1.0 if value > 0.0 else -1.0) / radius
        steps = max(1, int(math.ceil(length / 25.0)))
        for step in range(steps):
            travel = length * step / steps
            if kind == "straight":
                px = x + math.sin(heading) * travel
                py = y + math.cos(heading) * travel
                ph = heading
            else:
                hand = 1.0 if value > 0.0 else -1.0
                centre_x = x + math.cos(heading) * hand * radius
                centre_y = y - math.sin(heading) * hand * radius
                ph = heading + hand * travel / radius
                px = centre_x - math.cos(ph) * hand * radius
                py = centre_y + math.sin(ph) * hand * radius
            spline.append(
                {
                    "distance_m": round(distance + travel, PLACES),
                    "position": [round(px, PLACES), round(-py, PLACES)],
                    "heading_deg": round(math.degrees(ph), PLACES),
                    "curvature_1pm": round(curvature, 9),
                    "width_m": 8.0,
                    "crown_pct": 2.0 if curvature == 0.0 else 0.0,
                    "bank_pct": 0.0 if curvature == 0.0 else (5.0 if curvature > 0.0 else -5.0),
                    "elevation_m": 0.0,
                    "grade_pct": 0.0,
                    "segment": index,
                }
            )
        if kind == "corner":
            corner_spans.append((distance, distance + length, value))

    corners = []
    for number, (from_m, to_m, angle) in enumerate(corner_spans):
        half = math.radians(abs(angle)) * 0.5
        # rho = R + h(1 + cos(theta/2))/(1 - cos(theta/2)), h = (W - 1.400)/2
        h = (8.0 - KART_WIDTH_M) * 0.5
        rho = radius + h * (1.0 + math.cos(half)) / (1.0 - math.cos(half))
        grip = 3.6 * math.sqrt(1.86 * 9.80665 * rho)
        corners.append(
            {
                "name": "C%d" % (number + 1),
                "from_m": round(from_m, PLACES),
                "to_m": round(to_m, PLACES),
                "apex_arc_from_m": round(from_m, PLACES),
                "apex_arc_to_m": round(to_m, PLACES),
                "hand": "right" if angle > 0 else "left",
                "direction_change_deg": abs(angle),
                "min_radius_m": radius,
                "width_m": 8.0,
                "line_radius_m": round(rho, PLACES),
                "defender_line_radius_m": round(radius, PLACES),
                "grip_ceiling_kmh": round(grip, PLACES),
                "lock_ceiling_kmh": round(grip, PLACES),
                "apex_kmh": round(grip, PLACES),
                "reverse_apex_kmh": round(grip, PLACES),
                "runoff": {
                    "side": "left" if angle > 0 else "right",
                    "apron_m": 10.0,
                    "outfield_m": 20.0,
                    "outfield": "grass",
                    "barrier": "tire_type_c",
                    "sized_for": "forward",
                    "approach_kmh": round(grip + 15.0, PLACES),
                },
            }
        )

    checkpoint_count = int(math.ceil(total / CHECKPOINT_MAX_SPACING_M))
    return {
        "schema_version": SCHEMA_VERSION,
        "meta": {
            "name": "Self-intersecting negative control",
            "length_m": round(total, PLACES),
            "grade": 1,
            "net_turn_deg": round(math.degrees(end_heading), PLACES),
            "designed_from": "tools/circuits/author_track.py, build_self_intersecting()",
            "authored_by": "tools/circuits/author_track.py",
            "note": "DELIBERATELY BROKEN, in exactly one way: the road crosses itself. Everything else about it is legal, so a validator that reports anything but the crossing is finding the wrong thing. tools/verify/circuit.sh fails if this file loads.",
        },
        "spline": spline,
        "corners": corners,
        "elevation": {"vertical_curves": []},
        "surfaces": [],
        "furniture": {"start_line": {"distance_m": 0.0, "width_m": 0.30}},
        "layouts": [
            {
                "name": "forward",
                "direction": "forward",
                "sector_marks_m": [round(total / 3.0, PLACES), round(2.0 * total / 3.0, PLACES)],
                "checkpoints_m": [
                    round(i * total / checkpoint_count, PLACES) for i in range(checkpoint_count)
                ],
                "grid": {
                    "slots": 8,
                    "pole_side": "right",
                    "positions": [
                        {
                            "position": i + 1,
                            "distance_m": round(
                                (total - (GRID_FRONT_ROW_SETBACK_M + i * GRID_STAGGER_M)) % total,
                                PLACES,
                            ),
                            "lateral_m": (
                                GRID_COLUMN_OFFSET_M if i % 2 == 0 else -GRID_COLUMN_OFFSET_M
                            ),
                        }
                        for i in range(8)
                    ],
                },
                "racing_line_seed": [],
                "corner_speeds_kmh": [],
                "vertical_curve_speeds_kmh": [],
            }
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stem", default="valdirone_nuova")
    parser.add_argument("--out", default="data/tracks")
    parser.add_argument("--report", action="store_true")
    arguments = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    design_path = os.path.join(root, "docs", "circuits", "%s.json" % arguments.stem)
    with open(design_path, "r", encoding="utf-8") as handle:
        design = json.load(handle)

    track, report = author(design, arguments.stem)

    print("authored %s" % design["working_name"])
    print("  closure before the re-solve   %.6f m" % report["closure_before_m"])
    print("  largest length correction     %.6f m" % report["largest_length_correction_m"])
    print("  centerline length             %.4f m" % report["length_m"])
    # The design's own unrounded solve came out at 1375.1318 m. This one lands
    # 12 mm short of it, and that is not a disagreement about the geometry: the
    # design's nine straight lengths were published rounded to two decimals, the
    # weights of its own least squares are not recorded, and a minimum-norm
    # correction of the rounded values is the closest thing a reader can
    # reproduce. Both are inside the regulation's own 1 m accuracy for a
    # published circuit length (Part I art 11), and every corner - which is what
    # a lap time is made of - is bit-for-bit the design's.
    print("  design's own figure           %.4f m  (delta %+.4f m)"
          % (design["measured"]["total_length_m"],
             report["length_m"] - design["measured"]["total_length_m"]))
    print("  level band grade, solved      %.5f %%" % report["level_grade_pct"])
    print("  elevation closure             %.9f m" % report["elevation_closure_m"])
    print("  vertical curve lengths        %s" % ", ".join("%.3f" % v for v in report["curve_lengths_m"]))
    print("  control points                %d" % report["control_points"])
    if arguments.report:
        return 0

    out_dir = os.path.join(root, arguments.out)
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "%s.track.json" % arguments.stem)
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump(track, handle, indent=1, sort_keys=False)
        handle.write("\n")
    print("  -> %s" % os.path.relpath(out_path, root))

    broken_path = os.path.join(out_dir, "self_intersecting.track.json")
    with open(broken_path, "w", encoding="utf-8") as handle:
        json.dump(build_self_intersecting(), handle, indent=1, sort_keys=False)
        handle.write("\n")
    print("  -> %s (the negative control)" % os.path.relpath(broken_path, root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
