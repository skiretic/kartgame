## ADR-0053 — The pit lane is one piece of asphalt with four junctions, and the 30° cap was never in §7.4

**Status.** Accepted and implemented, ROADMAP M5. Issue #181. Extends
[ADR-0046](DECISIONS.md#adr-0046--trackjson-owns-the-whole-track-and-furniture-is-placed-by-distance)'s
"reverse is an authored layout, not a programmatic reversal" to the one part of a
circuit where that claim costs real geometry, and adds a fifth figure to
[ADR-0050](DECISIONS.md#adr-0050--the-starting-grid-is-ours-and-it-is-namespaced-so-it-reads-that-way)'s
`ours::` namespace under the same contract.

**Context.** M5 shipped `track.json` with `pit_entry_m` and `pit_exit_m` in both
layouts and **no asphalt anywhere**. The file's own `pit_note` said so, and so did
`circuit.gd`'s header. Three things were wrong with that state and only the first
was the obvious one.

The pit lane did not exist. Two stations in a file that nothing built from is the
same failure class `CLAUDE.md` already records as "a capability built at both ends
and not joined in the middle" — `assist_auto_shift` stored, bound and never
loaded — except that here neither end existed either. The loader validated that
the stations were on the lap and not inside a corner, which is a check that a
number is plausible, and then nothing read them.

**Both layouts carried the same two stations and the same note, and the note was
right about why that could not be enough.** A deceleration lane leaving at 22° to
the direction of travel is a 158° merge driven the other way, over the 30° cap,
whichever edge it is on. Forward, Valdirone's T8b and T1 are both left-handers, so
a kart on the line tracks out to the right at both junctions and the left edge is
free; reversed they are both rights and the free edge is the right one. Those are
the same physical edge — the circuit turns −360° net, so the inside of the loop is
the left of forward travel and the right of reverse travel — which means the
*lane* is shared and only the *junctions* are not. Nothing in the file could say
that, because a layout's `side` and the lane's `side` are in two different frames
and there was only one frame.

**And the 30° cap was cited to the wrong article in five places.** `CLAUDE.md`,
`scripts/game/circuit.gd`, `docs/TRACK_SCHEMA.md`, the Valdirone design document
and `track.json`'s own `pit_note` all said "Part I art 7.4's 30° cap". It is in
**§7.2**, in the *Characteristics* block, three sentences before the edge-line
rule this project already quotes from that same block. §7.4 is where the 3–4 m
lane width and the required entry chicane are. All five traced back to one
sentence written in a planning session and never checked against the text — the
same shape as the width that sat in Appendix 13 for a milestone, one level
shallower.

**Decision.** Five parts.

**1. The lane is furniture and the junctions are per layout.** `furniture.pit_lane`
carries `side` (in the forward frame, the same convention `surfaces[].side` uses),
`from_m`, `to_m`, `width_m` and `separation_m`. Each layout carries
`pit: { side, entry_angle_deg, exit_angle_deg }` with `side` in **that layout's own
frame**. Valdirone's forward layout reads `"left"` and its reverse layout reads
`"right"` and both name one edge of one road. That looks like a trap and is the
point: a file with one frame for both could not express the fact this whole issue
turns on.

**2. A junction is a gore, not a ribbon.** The stub is the wedge between the white
line and the lane's inner edge — zero wide at the junction, exactly `separation_m`
wide where it meets the lane. Laid instead as a 3.5 m ribbon over the taper it
would occupy the same band as the lane, and two coplanar collider faces along a
whole boundary is the condition that makes a suspension raycast's answer
arbitrary — the exact defect the run-off apron already caused once against the
verge. Gore inboard, lane outboard, and they cannot overlap because the gore's
offset never exceeds `separation_m` and the lane's never falls below it. That is
also what lets both be built unconditionally, for both layouts, with no
selected-layout state in either consumer: all five pieces are asphalt on the
ground whichever way the circuit is driven that session.

**3. The taper is derived and the angle is authored.** `taper = separation /
tan(angle)`, so the regulated quantity has exactly one home. 7.920 m at 22° and
11.160 m at 16°. An authored taper length is a second place for the angle to be
wrong and there is no way for a validator to tell which of the two is the lie.

**4. §7.2's no-crossing sentence is enforced as geometry.** *"Intersections … must
be located in such a way that there may be no crossing between the lines of karts
that are on the track and those of karts that enter the Repairs Area or leave
it."* A kart tracks out to the outside of the corner it is leaving and sets up on
the outside of the next, so the free edge is that corner's own **inside**, which is
its hand. The loader finds the corner most recently left and the corner about to be
entered, in the layout's own stations, and refuses a junction on the other edge.
`author_track.py` derives the sides the same way and raises rather than picking one
if a design's two junctions disagree.

**5. The separation is ours, and it is a sum of sourced numbers.** §7.4 refers the
servicing park's whole plan to **Appendix No. 9**, which is one of the three
appendices ADR-0050 already records as unpublished; probed again for this issue,
the file is a 404 and the regulations page serves a shell with no PDF links.
Nothing in either Part gives a pit-lane offset, a taper length or a speed-limit
line. So `circuit::ours::PIT_LANE_SEPARATION_M` is **3.20 m = §7.5's 1.80 m of
mandatory verge + Art. 8.1.1's 1.400 m kart width**, so that a kart which lands
squarely on its own verge is still short of the lane — with `_SOURCED = false`
beside it and the derivation in `docs/REFERENCES.md`.

**Not built: the chicane.** §7.4 requires one at the entry to the deceleration
lane and gives no length, no offset and no width — no geometry at all, anywhere in
the text. `circuit::PIT_ENTRY_CHICANE_REQUIRED` records it as
required-and-absent. Inventing one would put a fifth unsourced figure into a
namespace whose whole value is that its members are countable. Issue #184.

**Consequences.**

The two geometry consumers grew a fifth surface each and are measured against each
other on it. `gentrack.py`'s manifest carries 42 `pit_edges` rows — the lane's two
edges every 5 m, plus each gore's tip, midpoint and outboard corner, which are the
three stations where a taper's arithmetic can be wrong in three different ways.
`--case=agree`'s 56 centerline rows cannot see any of it: a collider built at the
wrong separation, on the wrong edge, or with its gores tapering backwards agrees
with every one of them and is a different circuit. Measured worst disagreement,
pit rows: **0.0005 mm**.

`circuit.sh --case=pit` is the gate and it carries **two** negative controls, both
built at run time from one-field edits of the real circuit: a 40° entry angle,
which must be refused naming §7.2's cap, and the reverse layout's stub moved onto
the far edge, which must be refused as two pit lanes. Run time rather than
committed because committing them would mean four track files to keep in step with
every schema bump. The side control fires four rules at once — both crossing rules
and both two-pit-lanes rules — which is the correct shape: one edit, one physical
mistake, four ways of noticing it.

**A pit lane is asphalt and a pit *limiter* is a station.** `surface_meshes()`
publishes `PitLane` as its own body with `surface_type` asphalt, because pit
asphalt is asphalt and a caller that asked the surface what road it was on would
get the true answer to the wrong question. `KartTrack::pit_lane()` and
`pit_stubs()` publish the stations, in the forward frame, for whatever M6 builds on
top.

**The lane's span is derived, not authored.** `author_track.py` takes the minimal
arc covering all eight gore stations — the complement of the largest cyclic gap,
which is 1,234.9 m of open circuit here — plus half a meter of overrun at each end.
An authored span is a number that drifts away from a changed angle, and what it
produces is a wedge of asphalt leading to grass.

**What did not change.** `pit_entry_m` and `pit_exit_m` keep their meaning and
their existing rule 20. The validator gained six rules, not a rewrite; the schema
version did not move, because nothing already in a `track.json` means anything
different now.
