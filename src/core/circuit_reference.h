#ifndef KART_CORE_CIRCUIT_REFERENCE_H
#define KART_CORE_CIRCUIT_REFERENCE_H

// Circuit dimensions the regulations fix, and the four figures they do not.
//
// This file is to `track.json` what `kz_reference.h` is to the vehicle and what
// `kz_audio_reference.h` is to the engine note: the single owner of every
// externally-sourced number the track schema rests on, with its provenance
// attached and its *unsourced* neighbors named rather than quietly filled in.
// `docs/REFERENCES.md`, "Circuit design and regulation", is the derivation and
// carries the PDFs and their hashes; this is the engineering face of it, and the
// two must not drift.
//
// Nothing in src/core/ may include godot-cpp. See docs/DECISIONS.md ADR-0017.
//
// ## Why this file exists at all
//
// `ARCHITECTURE.md` §11 listed most of a circuit's dimensions as design taste
// with one regulation cited in passing, and `track_ribbon.gd`'s 8 m width said in
// its own docstring that it was unsourced. Issue #157 closed that: nearly all of
// it is in the CIK-FIA text, and one figure the code had already used - the
// 120 mm edge line - was sourced with the source recorded nowhere. The remaining
// gap is the starting grid, and that gap is the reason this file separates
// `Sourced` from `Ours` in its namespaces rather than in its comments.
//
// ## What is sourced here, and what is not
//
// Sourced, to the 2026 Circuit Regulations Parts I + II and to Appendix No. 13,
// both listed with SHA-256 in `docs/REFERENCES.md` (refs T1 and T3):
//
//   - every Grade 1 track dimension: length, width, straight lengths, the two
//     start-line clearances, the separation between adjacent sections,
//   - the vertical-radius rule R = V^2/K and both K constants,
//   - camber, banking and the starting straight's gradient cap,
//   - the verge, the run-off up-slope cap and the gravel-bed minimums,
//   - the 80-degree threshold above which a run-off is mandatory,
//   - the edge-line width and the ban on any other paint.
//
// **Not sourced, and named as such rather than defaulted:** everything about the
// starting grid's geometry - slot pitch, lateral offset, stagger and the setback
// of the front row from the line. CIK-FIA **Appendices 9, 10 and 15** are
// referenced by the Part I text, are not published on fiakarting.com, and were
// not located. The Part I text's own appendix section reads, in full, *"Annexes -
// Voir fiakarting.com"*, which is the same trap that hid the track width in
// Appendix 13 for a milestone.
//
// So the four grid figures below are **ours**. They live in a separate namespace,
// each carries a `_SOURCED = false` beside it, and each is derived from a
// clearance a reader can check rather than chosen. That is the whole contract: a
// caller reaching for one reads why it is soft, and nobody six months from now
// has to guess which numbers came out of a PDF.

namespace kart::core::circuit {

// --- Grade 1 track dimensions ---------------------------------------------
//
// T3, *FIA Karting Circuit Licence Grades - Track Requirements*. Grade 1 is the
// requirement set for FIA Karting Championships, Cups and Trophies. The whole
// grade table is in `docs/REFERENCES.md`; only Grade 1 is here, because a circuit
// this project ships is a Grade 1 circuit or it is not interesting.

// Lap length, meters. Minimum only - the regulation sets no ceiling, and
// `ARCHITECTURE.md` §11's 1,000-1,500 m band is design taste on top of it.
inline constexpr double GRADE1_MIN_LENGTH_M = 1100.0;

// Track width, meters. **A floor, not a target.**
//
// A circuit held at 8 m the whole way round is the narrowest legal Grade 1 track.
// Widening a passing corner is free plausibility, and Valdirone Nuova spends it
// deliberately: 14 m at the hairpin and 9 m at Il Ciglione, where narrowness is
// the point. Against FIA Karting Art. 8.1.1's 1,400 mm maximum kart width, 8 m is
// 5.7 karts abreast and 14 m is 10.
inline constexpr double GRADE1_MIN_WIDTH_M = 8.0;

// The starting straight, meters: a band, not a minimum.
inline constexpr double GRADE1_STARTING_STRAIGHT_MIN_M = 120.0;
inline constexpr double GRADE1_STARTING_STRAIGHT_MAX_M = 200.0;

// The longest straight anywhere, meters.
//
// This is the constraint that decides where a kart circuit can pass. At the
// measured 1.53 g the deepest braking zone this kart can produce off a 200 m
// straight is about 45 m, so a passing place is made of a tow, an entry wide
// enough for two lines and an exit that punishes the defender - not of a long
// straight into a heavy braking zone, which is what §11 asked for and cannot have.
inline constexpr double GRADE1_LONGEST_STRAIGHT_MAX_M = 200.0;

// Clearances either side of the start line, meters.
inline constexpr double GRADE1_START_TO_FIRST_CORNER_MIN_M = 50.0;
inline constexpr double GRADE1_LAST_CORNER_TO_START_MIN_M = 70.0;

// Clear ground between two adjacent sections of track, meters. T1 §7.5: *"The
// minimum distance between two adjacent sections of the track is 6 m in any
// case."*
//
// **This is 6 m of clear ground, not 6 m between centerlines**, which is the
// distinction that makes a flat separation constant wrong the moment a circuit is
// wider than the 8 m floor. The requirement between two sections of half-width
// h_a and h_b is `6 + h_a + h_b`; at Valdirone's 14 m hairpin against an 11 m
// Rampa that is 18.5 m and not 14. Four separate circuit designs published a flat
// 14 m constant and all four would have passed an illegal layout.
inline constexpr double MIN_CLEAR_GROUND_BETWEEN_SECTIONS_M = 6.0;

// Kart capacity, from T3: L/28, capped at 36 for a race.
inline constexpr double METERS_OF_TRACK_PER_KART = 28.0;
inline constexpr int MAX_KARTS_IN_A_RACE = 36;

// --- Elevation, longitudinal ----------------------------------------------
//
// T1 §7.2, *Elevations*, **new in the 2026 text and absent from the 2025 text**:
// *"Any change in gradient should be affected using a minimum vertical radius
// calculated by the formula: R = V^2/K, where R is the radius in metres, V is the
// speed in km/h and K is a constant equal to 20 in the case of a concave profile
// or to 15 in the case of a convex profile. The value of R must be adequately
// increased along approach, release, braking and curved sections."*
//
// V is in **km/h** and R comes out in meters, so the constants below are not
// dimensionless and must not be used with speeds in m/s.
inline constexpr double VERTICAL_K_CONCAVE = 20.0;
inline constexpr double VERTICAL_K_CONVEX = 15.0;

inline constexpr double min_vertical_radius_m(double speed_kmh, double k) {
	return speed_kmh * speed_kmh / k;
}

// The most a vertical curve can ever load or unload a tire, in m/s^2.
//
// Substituting the regulation's own minimum radius into the centripetal term,
// with v = V/3.6, gives `a = (V/3.6)^2 / (V^2/K) = K/12.96` - **and the speed
// cancels**. So no legal Grade 1 kart circuit can swing tire load by more than
// +15.7% of static in a compression or -11.8% over a crest, at any speed, on the
// most aggressive profile the regulation permits, and §7.2 then says to increase R
// further in exactly the sections where a designer would want the effect.
//
// `ARCHITECTURE.md` §11's original "elevation change ... because it loads the
// tires" is wrong for a kart circuit for this reason, and the arithmetic that
// shows it is one line long. Elevation is a visibility and braking-distance
// device here and is not sold as anything else.
inline constexpr double MAX_VERTICAL_ACCEL_CONCAVE_MS2 = VERTICAL_K_CONCAVE / 12.96;
inline constexpr double MAX_VERTICAL_ACCEL_CONVEX_MS2 = VERTICAL_K_CONVEX / 12.96;

// The starting straight's gradient cap, percent. Same article.
inline constexpr double STARTING_STRAIGHT_MAX_GRADE_PCT = 2.0;

// --- Elevation, transverse ------------------------------------------------
//
// Same article: *"Along straights, the transversal incline, for drainage
// purposes, when measured between the two track edges or between the centreline
// and the track edge (camber), must not exceed 3% (1.7 deg) or be less than 1.5%
// (0.9 deg). In curves, the banking (cross camber) (downwards from the outside to
// the inside of the track) should not exceed 10% (5.7 deg). An adverse incline is
// not generally acceptable unless dictated by special circumstances. All changes
// in banking (cross camber) are to be made over an appropriate distance."*
//
// A straight is never flat: 1.5% is a floor and not a permission.
inline constexpr double STRAIGHT_CAMBER_MIN_PCT = 1.5;
inline constexpr double STRAIGHT_CAMBER_MAX_PCT = 3.0;
inline constexpr double CORNER_BANKING_MAX_PCT = 10.0;

// --- Run-off, verges and barriers -----------------------------------------

// T1 §7.5: the track *"must be bordered all along its length on both sides by
// compact verges having an even surface and having a minimum width of 1.80 m"*,
// continuing the track's transversal profile with no negative slope.
inline constexpr double VERGE_MIN_WIDTH_M = 1.80;

// Same article: a run-off that slopes up *"must not be in excess of 10%"*. The
// same cap applies to gravel beds, and it is the second half of the
// self-intersection gate - two sections of track that clear the 6 m horizontally
// can still be stacked at a slope no legal verge can bridge.
inline constexpr double RUNOFF_UPSLOPE_MAX_PCT = 10.0;

// T1 §8.2: gravel beds are minimum 2 m wide and minimum 300 mm thick, rolled at
// 5/15 granulometry, and *"must neither be located below the track level nor be
// preceded by a heightened verge"*.
inline constexpr double GRAVEL_BED_MIN_WIDTH_M = 2.0;
inline constexpr double GRAVEL_BED_MIN_DEPTH_M = 0.30;

// T1 §7.5: a run-off *"is mandatory to build one in the axis of kart lines with a
// change of direction of over 80 deg"*.
//
// **Measured on the corner, not on the segment.** Valdirone's T5 Vigna is a 24 deg
// arc into a 71 deg arc with no straight between them; both clear 80 on their own
// and the compound is 95 deg and mandatory. A validator written against the
// control-point list rather than against the corner list would miss it, which is
// why `track.json` carries a corner list at all.
inline constexpr double RUNOFF_MANDATORY_OVER_DEG = 80.0;

// --- The pit lane ---------------------------------------------------------
//
// **The merge cap is in §7.2 and not in §7.4**, and this project cited it as 7.4
// in five places before the text was read line by line. §7.2's *Characteristics*
// block, immediately before the edge-line sentence this file already quotes:
// *"The angle of the deceleration lane and of the pit exit lane relative to the
// track must not exceed 30 deg."*
//
// The same block carries the rule the *side* of a junction has to satisfy, and it
// is a geometry rule rather than a caption: *"intersections of deceleration and
// exit lanes relating to the track must be located in such a way that there may be
// no crossing between the lines of karts that are on the track and those of karts
// that enter the Repairs Area or leave it."* A kart on the line tracks out to the
// **outside** of the corner it is leaving and sets up on the outside of the corner
// it is entering, so the edge that is free at a junction is the corner's own
// **inside**, and `track.h` checks a stub's side against exactly that.
inline constexpr double PIT_MERGE_MAX_DEG = 30.0;

// §7.4, *Servicing Parks and Parc Ferme*: *"The width of the deceleration lane
// must be between 3 m and 4 m."* That is the only pit dimension anywhere in the
// text - the lane's own width, its length, any speed-limit line and the servicing
// park's plan are all in **Appendix No. 9**, which is not published (see the
// `ours::` block below, and ADR-0050 for the identical hole under the grid).
inline constexpr double PIT_LANE_WIDTH_MIN_M = 3.0;
inline constexpr double PIT_LANE_WIDTH_MAX_M = 4.0;

// Same article: *"There must be a chicane at the entry to the deceleration lane
// aimed at reducing the speed of the karts."*
//
// A constant rather than a comment because it is a rule with **no geometry
// attached anywhere in the text**, so the honest state of it is "required, not
// built" rather than a chicane invented to fill the sentence. Issue #184.
inline constexpr bool PIT_ENTRY_CHICANE_REQUIRED = true;

// --- Paint ----------------------------------------------------------------

// T1 §7.2: the track edges are delimited by white or yellow lines *"with a maximum
// width of 120 mm"*. `track_ribbon.gd` uses 100 mm and is inside it.
inline constexpr double EDGE_LINE_MAX_WIDTH_M = 0.120;

// T1 §12: *"Any paint on the circuit surfacing, other than that which delimits the
// edges of the track and determines the starting grid, is forbidden for safety
// reasons."*
//
// A constant rather than a comment because it is a rule a *generator* has to obey:
// `gentrack.py` may emit edge lines, the start line and the grid boxes, and
// nothing else. Corner numbers and braking boards go on free-standing structures
// behind the verge.
inline constexpr bool PAINT_LIMITED_TO_EDGES_AND_GRID = true;

// T1 §14.6: *"Kerbs must be painted in two colours alternately (recommended
// colours: red and white)."* That is the whole of it. The regulations specify a
// kerb's paint, its inspection and its repair, and **say nothing about its
// profile, height, width or stripe pitch** - so the 30 mm height and the 1 m
// alternation this project draws are choices consistent with the rule and are
// bracketed in `track_ribbon.gd` by the kart rather than by a document.

}  // namespace kart::core::circuit

// --- Ours -----------------------------------------------------------------
//
// Everything below this line is a figure this project chose. None of it is in any
// CIK-FIA document that could be located, and every one of them is separated into
// its own namespace so that `circuit::` reads as "sourced" at every call site and
// `circuit::ours::` reads as "we picked this".
//
// The rule they are recorded under is `ARCHITECTURE.md` §5 item 10, widened after
// ADR-0034: an externally-sourced constant invented in a planning session is
// indistinguishable from a measured one six months later, and this project has now
// paid for that twice - §6.4's lateral band and the engine's harmonic ladder. The
// grid was about to be the third.

namespace kart::core::circuit::ours {

// ## The starting grid, and why none of it is sourced
//
// CIK-FIA Part I §7.7 describes the starting *procedure* and the light gantry, and
// for the grid itself refers to **Appendix No. 10**. Appendices 9, 10 and 15 are
// referenced by the Part I text and are not published on fiakarting.com; the
// appendix section of Part I reads *"Annexes - Voir fiakarting.com"* and the site
// does not carry them. Appendix 13, which *is* published, is a licence-criteria
// table and says nothing about slot geometry.
//
// So all four numbers below are ours. They are not arbitrary: each is derived from
// a clearance that can be checked against the kart, and the derivations are here so
// that a real figure, when one turns up, can be dropped in and the consequences
// read off. The kart is 1.400 m wide (FIA Karting Art. 8.1.1's overall maximum,
// which this project's rear track already is) and 1.830 m long (the CIK maximum,
// and the M2 gate measures the generated kart at 1.830 m).

// Longitudinal pitch between rows in the same column, meters.
//
// 8.0 m is 4.37 kart lengths and leaves **6.17 m of clear road** between the tail
// of one kart and the nose of the one behind it in the same column. The figure
// that actually matters is the staggered one below - a kart's real clearance ahead
// is half this - and 8.0 was chosen so that half of it is still comfortably more
// than a kart length.
inline constexpr double GRID_ROW_PITCH_M = 8.0;
inline constexpr bool GRID_ROW_PITCH_SOURCED = false;

// How far the second column sits behind the first, meters.
//
// Exactly half the row pitch, so successive grid positions alternate sides at a
// uniform 4.0 m and **no kart sits directly behind another**. That leaves
// 4.0 - 1.830 = **2.17 m of clear road** ahead of every kart on the grid, which is
// the number this constant exists to set: a KZ leaves the line on a centrifugal
// clutch and a dry-clutch launch that bogs needs room ahead of it that is not
// somebody's rear bumper.
inline constexpr double GRID_STAGGER_M = GRID_ROW_PITCH_M * 0.5;
inline constexpr bool GRID_STAGGER_SOURCED = false;

// Lateral offset of each column from the centerline, meters.
//
// +/-3.0 m on Valdirone's 12 m start straight: 6.0 m between column centerlines,
// so **4.6 m of clear air between two karts side by side** and 2.3 m from each
// kart's outer edge to the white line. Stated as an offset rather than as a
// fraction of the width on purpose - a grid on a narrower start straight should
// fail the fit check and be re-authored, not silently squeeze.
inline constexpr double GRID_COLUMN_OFFSET_M = 3.0;
inline constexpr bool GRID_COLUMN_OFFSET_SOURCED = false;

// How far the front row sits behind the start line, meters.
//
// 4.0 m, one stagger. Small on purpose: everything behind the front row is spent
// out of the 120-200 m the starting straight is allowed to be, and Valdirone's
// grid of eight already occupies 28.0 m of a 165 m straight.
inline constexpr double GRID_FRONT_ROW_SETBACK_M = 4.0;
inline constexpr bool GRID_FRONT_ROW_SETBACK_SOURCED = false;

// The length of grid a slot count needs, meters. `slots` karts alternating sides
// at `GRID_STAGGER_M`, plus the setback, plus one kart length behind the last row
// so the back of the grid is on the road rather than hanging off it.
inline constexpr double grid_length_m(int slots) {
	return GRID_FRONT_ROW_SETBACK_M + (slots > 0 ? (slots - 1) * GRID_STAGGER_M : 0.0) + 1.830;
}

// The width a two-column grid needs, meters: both columns plus a kart's width plus
// the two edge lines it must stay inside of.
inline constexpr double GRID_MIN_TRACK_WIDTH_M = 2.0 * GRID_COLUMN_OFFSET_M + 1.400 + 2.0 * 0.120;

// ## Superelevation runoff, meters
//
// The regulation says all changes in banking are *"to be made over an appropriate
// distance"* and gives no distance. This is that distance, and it is ours.
//
// 20 m is 0.52 s at 140 km/h and 1.4 s at 52 km/h, so the fastest banking change on
// Valdirone develops in about half a second - fast enough to be felt as the corner
// arriving rather than as a step, and slow enough that the roll rate it commands is
// well inside what the chassis can follow. It is clamped to half of each adjacent
// span, so a short arc between two different bankings ramps over what it has rather
// than overlapping the neighbouring ramp.
inline constexpr double BANKING_TRANSITION_M = 20.0;
inline constexpr bool BANKING_TRANSITION_SOURCED = false;

// ## Clear ground between the track's asphalt and the pit lane's, meters
//
// The regulation gives the deceleration lane's *width* (3-4 m, §7.4) and the
// *angle* it may leave at (30 deg, §7.2) and says nothing whatever about how far
// from the track it then runs. The servicing-park plan that would say is
// **Appendix No. 9**, referenced by §7.4 and not published - the same hole as the
// grid's Appendix 10 in ADR-0050, probed again for this issue and still a 404 on
// fiakarting.com. So this figure is ours.
//
// It is the sum of two sourced numbers and it is stated that way so a reader can
// check it rather than take it: **§7.5's 1.80 m of mandatory verge, which the pit
// lane may not eat, plus FIA Karting Art. 8.1.1's 1.400 m kart width**, so that a
// kart which leaves the track sideways and lands squarely on its own verge is
// still short of the pit lane's asphalt. 3.20 m.
//
// Consequence worth reading off: with the taper set by the branch angle, the
// junction gore is `3.20 / tan(theta)` long - 7.92 m at 22 deg and 11.16 m at
// 16 deg. Widen the separation and both gores lengthen proportionally, which is
// the trade a real figure would settle.
inline constexpr double PIT_LANE_SEPARATION_M = VERGE_MIN_WIDTH_M + 1.400;
inline constexpr bool PIT_LANE_SEPARATION_SOURCED = false;

// ## Checkpoint spacing, meters
//
// Not a regulation at all - the FIA has no opinion on how a game validates a lap.
// This is the anti-cut resolution: a kart that skips a checkpoint has not completed
// the lap, so the spacing bounds how much of the circuit a driver could rejoin past
// and still be credited.
//
// 100 m is under one second of open-road running at Valdirone's 140.8 km/h peak and
// is 7.3% of the lap, so no shortcut that saves meaningful time can avoid deleting
// at least one. Tighter costs nothing but is pointless; looser starts to admit a
// cut across the infield.
inline constexpr double CHECKPOINT_MAX_SPACING_M = 100.0;
inline constexpr bool CHECKPOINT_MAX_SPACING_SOURCED = false;

}  // namespace kart::core::circuit::ours

#endif  // KART_CORE_CIRCUIT_REFERENCE_H
