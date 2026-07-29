# 98. Corrections the build found in this spec

The spec was written before any of it was built, which is the whole point of #190
— but it means the build is the first thing that ever measured it. This section
records every place a build wave found the spec wrong, with the arithmetic, so the
document does not quietly disagree with the kart it produced.

**None of these is a case of the spec being wrong about intent.** All of them are
arithmetic, a stale premise, or a number read off a chassis that a later wave moved.
That distinction matters: it is evidence the method worked and the transcription
did not, which is a much cheaper class of defect than the one #190 was written to
kill.

## 98.1 Arithmetic

**§50.9's steering-wheel top edge used `sin` where the geometry wants `cos`.**
A disc raked θ from vertical has its highest rim point at `center_z + r·cos θ`:

    480 + 160 * cos(0.470) = 480 + 160 * 0.8917 = 622.7      correct
    480 + 160 * sin(0.470) = 480 + 160 * 0.4529 = 552.5      what §50.9 wrote

The front panel clears Art. 9.5.3's *"not […] above the horizontal plane defined by
the top of the steering wheel"* under either figure, so nothing built came out
wrong — but the stated hands clearance was also understated. Measured to the nearest
rim point at (±137.5, 357, 553) the gap is **237 mm**, not the 166 the spec derived,
against Art. 9.5.3's 50 mm minimum.

**§50.8 said the fairing at a 742 mm lip is "entirely forward of the loop".** The
front loop's frontmost tube surface is at y **+775**, so 742 is 33 mm *behind* it.
Built: the lower skin's rear edge goes to +782 and the underside is shallower than
the top, 247 mm against 287 — which is what a real fairing does, and for this
reason.

**§10.7 said `nose_width` 680 was "clamped to 0.512".** The constant is
`NOSE_HALF_WIDTH_LIMIT = 0.256`, a *half* width, so the parameter block and the mesh
disagreed by **168 mm**, not 178.

## 98.2 Stations read off a chassis that wave 1 then moved

This is the predictable cost of specifying six assemblies in parallel against a
chassis that was itself being respecified. Each is a premise change, not a reasoning
error.

| spec | said | actual after wave 1 | error |
| --- | --- | --- | --- |
| §20.6.5 rear disc | walked out from a hanger plate at −185 → disc at −260 | plate is at **−300**, so the disc lands at **−400** and sprocket separation is 515, not 375 | 140 |
| §50.11 rear support | rail end at ±215 | rail is at **±310** | 95 inboard |
| §50.8 U-frame rear clamp | (±225, +545) | the loop's leg at that x is at y **+606** | 61 mm of air |
| §30.2 mount / brackets | rail pinching 285 → 245, 7.13° in plan | rail is **straight at x 310**, 0° in plan | 36–47 |

The last one was caught by the recheck pass before it reached a build. The first
three were caught by the wave that had to hit them.

**The general rule this produces:** a spec station that references a chassis feature
must be re-measured against the **built** frame, not carried from the spec's prose.
Waves 2 and 3 were briefed to do exactly that.

## 98.3 A built straight is not the length of its control polyline

`frame._corner` solves a fillet's tangent and pushes the corner control point
outward, so an authored polyline and the built tube are different objects:

    upper bumper bar, authored corner x   192.5
    upper bumper bar, built corner x      248.6      +56.1

A consumer interpolating a bar's position between its authored straight length and
its mount spacing is therefore up to **41 mm** out. Wave 2 lost a fairing strut to
this by 28 mm before measuring the tube instead. Anything picking up on a bumper
measures the built mesh.

Related, and the same family: a bar whose control polyline is 305 mm long has only
**191 mm of built straight** at `bend_radius` 60, because the fillet eats 57 mm at
each end. Art. 9.4.1 measures the straight, so `frame._corner` asserts rather than
letting a clamped fillet silently shorten a regulated run.

And a Ø35 aperture measures **36.2 mm** across its chamfered mouth at high detail,
because `build.bevel_object` runs a 4 mm offset on a 4 mm pan. Art. 4.6 caps it at
35, so the hole is authored at **33**.

## 98.4 One waiver that geometry will not let close

Wave 2 could not close `bodywork_rear_panel` against the frame, and the reason is a
genuine over-constraint rather than sloppiness:

    rear protection front face   y -705   fixed by a <=400 overhang cap
                                          and a sourced 187 mm panel depth
    returned lip needs                    10 mm behind that free edge
    chassis_cross_tail front     y -702   3 mm FORWARD of the panel
    chassis_rear_bumper legs     y -715   exactly on the fold's end

No z band escapes either: the cross member sits at z 39–61 against a 40 mm window
edge, and the bumper legs sweep 50–140 against a 95 mm edge. Three fixes exist, all
of them the chassis's:

1. `cross_tail_y` back 12 mm — `params.py`'s own docstring already records −724 as
   its other reading.
2. Root the bumper legs at `rail_rear_y` instead of 20 mm forward of it.
3. `overhang_rear_protection` down to 352, which clears both but spends 15 of the
   tire gap's 35 mm of margin.

Waived with its measurement rather than resolved by whichever wave noticed it,
because a part moving to suit a neighbor is the exact failure #190 exists to stop.

## 98.5 Six parameters the coverage gate found that the spec did not

§10.7 named three dead parameters — a field no module reads. The build's new
coverage assertion found six more, and every one had been **restated as a literal**
in the module that should have read it:

    engine_width  engine_length  engine_y  engine_z
    exhaust_max_diameter  pedal_length

`powertrain.SPROCKET_Z = 0.150` *is* `engine_z`, and its comment says so in words.
`cockpit.PEDAL_ARM_LENGTH = 0.120` is `pedal_length`. `powertrain.py`'s module
docstring claims to read seven parameters and reads three.

That is the same defect as `length_overall` seen from the other end: there, a number
nobody sourced became load-bearing; here, a number with a single owner was copied
until the owner stopped mattering.
