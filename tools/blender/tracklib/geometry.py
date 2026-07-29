"""`track.json`'s geometry, in Python, for the Blender half of the pipeline.

This is a **second implementation of one specification**, not a shared library.
`src/core/track.h` is the first; `docs/TRACK_SCHEMA.md` is the specification both
of them implement. There is no way to share the code - one is C++ inside the
engine and the other is Python inside Blender - so the only thing that keeps them
honest is that the rules are written down in arithmetic and that
`tools/verify/circuit.sh` measures the two against each other rather than
assuming.

`ARCHITECTURE.md` §11 states the goal in one line: because both consumers read the
same definition, what you see and what you collide with cannot drift apart. That
sentence is only true if something checks, and `--manifest` below is what it
checks.

Nothing in here imports `bpy`. That is on purpose - it is what lets the geometry
be exercised from an ordinary `python3` without Blender, which is how the
manifest cross-check runs in CI-shaped environments that have no Blender at all.
"""

from __future__ import annotations

import json
import math

#: The schema version this pipeline speaks. Refuses rather than migrates, the
#: same as `KartTrack::SCHEMA_VERSION` and for ADR-0046's reason: a track is
#: authored project data under version control, so a schema bump is a commit that
#: edits the tracks in the same commit and a loud failure is correct.
SCHEMA_VERSION = 1


class Frame:
    """A point on the centerline: everything a consumer needs about it."""

    __slots__ = (
        "distance_m", "x", "z", "elevation_m", "heading_rad", "curvature",
        "grade", "width_m", "crown_pct", "bank_pct",
    )

    def __init__(self, **fields):
        for name in self.__slots__:
            setattr(self, name, fields.get(name, 0.0))

    def forward(self) -> tuple[float, float]:
        return math.sin(self.heading_rad), -math.cos(self.heading_rad)

    def right(self) -> tuple[float, float]:
        return math.cos(self.heading_rad), math.sin(self.heading_rad)

    def surface_point(self, lateral: float, lift: float = 0.0) -> tuple[float, float, float]:
        """The road's surface at a lateral offset, in Godot's frame.

        `docs/TRACK_SCHEMA.md`'s one cross-section formula:

            y(u) = elevation - (crown/100)|u| - (bank/100)u

        so y(0) is the control point's elevation, a crown falls away on both
        sides, and a positive bank falls to the right of travel. It is piecewise
        linear in u, which is why three columns of vertices reproduce it exactly
        rather than approximately - in the mesh and in the collider both.
        """
        rx, rz = self.right()
        y = (
            self.elevation_m
            - self.crown_pct * 0.01 * abs(lateral)
            - self.bank_pct * 0.01 * lateral
        )
        return self.x + rx * lateral, y + lift, self.z + rz * lateral


class Track:
    """A loaded `track.json`. Geometry only; validation lives in `src/core/track.h`."""

    def __init__(self, path: str):
        with open(path, "r", encoding="utf-8") as handle:
            self.raw = json.load(handle)
        version = self.raw.get("schema_version")
        if version != SCHEMA_VERSION:
            raise SystemExit(
                "gentrack: %s is schema_version %r and this pipeline speaks %d. "
                "A track is edited by the commit that bumps the schema, not "
                "migrated at load (ADR-0046)." % (path, version, SCHEMA_VERSION)
            )
        self.path = path
        self.meta = self.raw["meta"]
        self.name = self.meta["name"]
        self.length_m = float(self.meta["length_m"])
        self.points = self.raw["spline"]
        self.corners = self.raw.get("corners", [])
        self.surfaces = self.raw.get("surfaces", [])
        self.layouts = self.raw.get("layouts", [])
        self.starts = [float(p["distance_m"]) for p in self.points]

    # --- geometry ---------------------------------------------------------

    def span_length(self, index: int) -> float:
        following = (index + 1) % len(self.points)
        if following == 0:
            return self.length_m - self.starts[index]
        return self.starts[following] - self.starts[index]

    def wrap(self, distance: float) -> float:
        return distance % self.length_m

    def span_at(self, distance: float) -> int:
        distance = self.wrap(distance)
        low, high = 0, len(self.points) - 1
        while low < high:
            middle = (low + high + 1) // 2
            if self.starts[middle] <= distance:
                low = middle
            else:
                high = middle - 1
        return low

    def sample(self, distance: float) -> Frame:
        """Exact. An arc's point comes from its centre and its own final heading.

        Sampling density therefore changes what is drawn and never what the road
        *is*. That matters most where it is least visible: Valdirone's hairpin is
        a 15 m radius, and sampled at any spacing coarse enough to be cheap its
        radius would become a function of the sampling rate - and the radius is
        what decides whether the corner is on the safe side of issue #137.
        """
        distance = self.wrap(distance)
        index = self.span_at(distance)
        a = self.points[index]
        b = self.points[(index + 1) % len(self.points)]
        span = self.span_length(index)
        travel = distance - float(a["distance_m"])
        t = travel / span if span > 0.0 else 0.0

        heading = math.radians(float(a["heading_deg"]))
        curvature = float(a["curvature_1pm"])
        ax, az = float(a["position"][0]), float(a["position"][1])
        here = heading + curvature * travel
        if curvature == 0.0:
            x = ax + math.sin(heading) * travel
            z = az - math.cos(heading) * travel
        else:
            radius = 1.0 / curvature
            centre_x = ax + math.cos(heading) * radius
            centre_z = az + math.sin(heading) * radius
            x = centre_x - math.cos(here) * radius
            z = centre_z - math.sin(here) * radius

        grade_a = float(a["grade_pct"]) / 100.0
        grade_b = float(b["grade_pct"]) / 100.0
        # Cubic Hermite on (elevation, grade). Exact for a parabolic vertical
        # curve, because a parabola is a cubic and the interpolant is the unique
        # cubic matching four constraints - so a circuit's whole longitudinal
        # profile needs two control points per curve and is then exact everywhere.
        t2 = t * t
        t3 = t2 * t
        elevation = (
            (2.0 * t3 - 3.0 * t2 + 1.0) * float(a["elevation_m"])
            + (t3 - 2.0 * t2 + t) * span * grade_a
            + (-2.0 * t3 + 3.0 * t2) * float(b["elevation_m"])
            + (t3 - t2) * span * grade_b
        )
        grade = (
            ((6.0 * t2 - 6.0 * t) * float(a["elevation_m"])
             + (-6.0 * t2 + 6.0 * t) * float(b["elevation_m"])) / span
            + (3.0 * t2 - 4.0 * t + 1.0) * grade_a
            + (3.0 * t2 - 2.0 * t) * grade_b
        ) if span > 0.0 else grade_a

        def lerp(key: str) -> float:
            return float(a[key]) + (float(b[key]) - float(a[key])) * t

        return Frame(
            distance_m=distance, x=x, z=z, elevation_m=elevation,
            heading_rad=here, curvature=curvature, grade=grade,
            width_m=lerp("width_m"), crown_pct=lerp("crown_pct"),
            bank_pct=lerp("bank_pct"),
        )

    def polyline(self, sagitta: float = 0.02, max_spacing: float = 2.0) -> list[Frame]:
        """The centerline, subdivided to a chord tolerance rather than a count.

        A chord subtending `t` radians of radius `R` leaves `R(1 - cos(t/2))`,
        which is `R t^2 / 8` for small `t`; inverting it gives the angular step.
        Stated as a tolerance because this circuit's radii differ by a factor of
        five between the 15 m hairpin and T8b's 70 m, and a fixed count would make
        one polygonal and the other wasteful.
        """
        out: list[Frame] = []
        for index in range(len(self.points)):
            span = self.span_length(index)
            curvature = float(self.points[index]["curvature_1pm"])
            step = max_spacing
            if curvature != 0.0:
                radius = abs(1.0 / curvature)
                step = min(max_spacing, math.sqrt(8.0 * sagitta / radius) * radius)
            steps = max(1, int(math.ceil(span / step)))
            for sub in range(steps):
                out.append(self.sample(self.starts[index] + span * sub / steps))
        # The closing sample is the first one again, so the ribbon's seam is
        # watertight rather than merely close. A seam that disagrees in the last
        # bit is a hairline crack in the road for a suspension raycast to find.
        closing = self.sample(0.0)
        closing.distance_m = self.length_m
        out.append(closing)
        return out

    # --- surfaces ---------------------------------------------------------

    def spans_covering(self, span: dict, near: Frame, far: Frame) -> bool:
        """Does a surface span cover this pair of samples?

        A span that wraps past the start line is two spans on the lap; both halves
        are tested rather than one comparison that silently drops the piece before
        zero. Valdirone has one - T2's exit strip runs off the end of the corner.
        """
        low = float(span["from_m"])
        high = float(span["to_m"])
        if low <= high:
            return near.distance_m >= low - 1e-6 and far.distance_m <= high + 1e-6
        return near.distance_m >= low - 1e-6 or far.distance_m <= high + 1e-6

    @staticmethod
    def ramp(span: dict, distance: float, ramp_length: float = 4.0) -> float:
        """Zero at either end of a kerb, one in the middle.

        Ramped along the direction of travel only. The **lateral** face stays
        vertical at full height for the whole kerb, because that face is the one
        issue #139 wants driven at - a driver who meets the *end* of a kerb head-on
        at 140 km/h is meeting a 30 mm step at zero degrees of incidence, which is
        a modeling artifact rather than a test.
        """
        nearest = min(distance - float(span["from_m"]), float(span["to_m"]) - distance)
        return max(0.0, min(nearest / ramp_length, 1.0))
