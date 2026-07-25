extends SceneTree

## Issue #31 — measure what Jolt does at a *contact*, before M3b's per-wheel
## spring is built on an assumption about it.
##
##     godot --headless --path . --script tools/verify/contact_probe.gd
##     godot --headless --path . --script tools/verify/contact_probe.gd -- --stall
##     godot --headless --path . --script tools/verify/contact_probe.gd -- --hz=240
##
## Deterministic: two runs in two processes differ only in the two lines that are
## labeled as wall-clock durations. No shell wrapper, for the same reason
## `integration_probe.gd` has none — there is nothing to wrap but the ADR-0018
## double import, and that is `verify.sh`'s job.
##
## ## Why this file exists
##
## ADR-0032 measured the force-application boundary and closed `ARCHITECTURE.md`
## §19's first risk — but it measured a body in **free space**, and said so in its
## own "What this does not settle":
##
##     Everything with a contact in it. This measures a body in free space; the
##     wheel raycasts, the friction solver and the resting contact behavior are
##     not covered, and a force applied at a contact point that Jolt is
##     simultaneously resolving is a harder question.
##
## This is that harder question, asked the same way: **a probe with an analytic
## answer for every question it asks.** Every measured number below is printed
## next to the number theory predicts for it, and where two theories are
## plausible — a free body versus a unilateral contact, an offset measured from
## the body origin versus from the center of mass, a world-space lever arm versus
## a body-local one — *both* candidate answers are printed so the reader can see
## which one the engine landed on rather than being told.
##
## That discipline is not decoration. Four separate M3a defects were all
## plausible-but-wrong assumed engine behavior (ADR-0031, ADR-0030), and issue
## #107 is the worked example of a correct set of numbers producing a wrong
## conclusion because the conclusion rested on a model instead of a measurement.
##
## ## The seven questions
##
##   1. **Resting contact.** Is the normal force Jolt applies exactly `m·g`?
##      What is the residual velocity and the penetration depth at steady state,
##      and does either depend on mass? A wheel spring computed from a raycast
##      distance is meaningless if the body's rest height is not stable.
##   2. **A force applied at a contact Jolt is resolving.** Push up at the
##      contact patch with `k·m·g` for k from well below 1 to well above. Does
##      the response equal the free-body `(F − m·g)/m` or the unilateral-contact
##      `max(0, (F − m·g)/m)`, and how sharp is the transition? A wheel spring
##      lives exactly at that transition.
##   3. **Jolt's own friction versus applied lateral force.** The M3b solver
##      applies *all* tire force itself, so any friction Jolt contributes is
##      double-counting. How much lateral force does it take to move a resting
##      body, what does `physics_material_override.friction` do to that, and what
##      is the combine rule between body and ground?
##   4. **Raycast stability and cost.** Is `intersect_ray`'s distance exactly the
##      analytic one, is the normal exact, does the ray hit the body's own
##      collider without `exclude`, does a fast body change the answer, and what
##      happens to a ray that *starts inside* geometry — which is what a
##      suspension ray does the moment a wheel is buried in a curb.
##   5. **Offset force and a custom inertia tensor.** Does `apply_force(F, r)`
##      produce `α = I⁻¹(r × F)`? Is `r` measured from the origin or from the
##      center of mass, and is it a world vector or a body-local one? The vehicle
##      sets its own inertia from a box approximation, so this is load-bearing.
##   6. **Does ADR-0032's conclusion 3 survive a contact?** Four applications of
##      `F/4` equal one of `F` in free space. That is what the 240 Hz substepping
##      does every tick, and every tick it does it the kart is on the ground.
##   7. **`max_physics_steps_per_frame`.** ADR-0032 flagged it unmeasured. Under
##      `--stall` this file induces a frame-rate collapse and measures how far
##      simulation time falls behind wall-clock.
##
## ## Rules this file keeps
##
## Deterministic. Nothing the measurement depends on reads wall-clock time —
## every quantity is a function of tick count and physics state. Two exceptions,
## both labeled where they are printed and neither gating anything: the raycast
## *cost* in question 4, which is a duration and cannot be anything else, and the
## whole of question 7, which is a measurement *of* wall-clock behavior and only
## runs when `--stall` is passed.
##
## Every case runs in one world at the same time, spaced along X, so a difference
## between two cases cannot be a difference in anything else. Horizontal pushes
## are all along +Z, across the lanes, so a body that slides never reaches its
## neighbor.
##
## The answers this produced become an ADR. The file is kept so the same
## questions can be re-asked of a future engine in one command, which matters
## because every answer here is an engine behavior and not a specification —
## exactly the argument ADR-0032 makes for keeping `integration_probe.gd`.

# --- geometry and layout ---------------------------------------------------

## Half-extent of every probe box, meters. A 0.5 m cube. The analytic rest height
## of its center on a plane at y = 0 is exactly this number, which is what makes
## penetration directly readable as `rest_y − HALF_EXTENT`.
const HALF_EXTENT := 0.25

## Lane spacing along X, meters. Wide enough that nothing in one lane can touch
## anything in the next: the largest vertical excursion below is ~20 m and the
## largest horizontal one ~40 m, both along axes that are not X.
const LANE_SPACING := 8.0

## Ground size, meters, square. Sized so every sliding body stays over it — and
## the "silently" in that sentence is not rhetorical. An earlier revision of this
## file added seven cases, which pushed the last lane 4 m past a 600 m ground,
## and the raycast section quietly reported 240 misses on rays whose analytic
## answer was 2.000000 m. `_check_layout` now asserts the lanes fit.
const GROUND_SIZE := 1200.0
const GROUND_THICKNESS := 2.0

## Surface friction, matching `scripts/game/proving_ground.gd`. The friction
## question is only useful if the ground under the probe is the ground under the
## kart.
const GROUND_FRICTION := 1.0

# --- timing ----------------------------------------------------------------

## Ticks spent letting every body settle onto the plane before anything is
## pushed. 240 at 120 Hz is 2 s, which is far longer than a 0.5 m box needs and
## cheap enough not to matter.
const TICKS_SETTLE := 240

## The tail of the settle phase over which the resting statistics are gathered.
## Taking the last second rather than the last tick is what distinguishes "at
## rest" from "at the bottom of a slow oscillation".
const TICKS_REST_WINDOW := 120

## Ticks the forces are applied for. 2 s, long enough that the smallest
## interesting acceleration in the sweep — 0.01 g at k = 1.01 — moves the body
## 0.196 m, which no amount of contact noise can hide.
const TICKS_MEASURE := 240

# --- masses and forces -----------------------------------------------------

## The kart's mass with driver, from `ARCHITECTURE.md` §6 via
## `kart_debug_vehicle.gd`. Deliberately *not* a round legibility mass like
## `integration_probe.gd`'s 10 kg: a solver's steady-state penetration and its
## speculative-contact distance are absolute lengths in meters, not quantities
## that scale with mass, so measuring them at 10 kg would understate what a
## 175 kg kart sees. The mass sweep in question 1 tests that claim rather than
## assuming it.
const KART_MASS := 175.0

## Height of the second, lower-friction slab, meters. Its top face sits here so
## that three bodies can rest on a surface whose friction is *not* 1.0, which is
## the only way to tell Jolt's friction combine rule apart: with a ground of 1.0
## every candidate rule (min, product, sqrt of product) agrees, and only a second
## surface separates them.
const SLAB_TOP := 1.0
const SLAB_FRICTION := 0.5

## Body frictions tested against `SLAB_FRICTION`. The five plausible combine
## rules predict five different effective coefficients for 0.25 against 0.5 —
## min 0.25, product 0.125, sqrt-of-product 0.354, max 0.5, mean 0.375 — so one
## sliding body decides it.
const COMBINE_FRICTIONS: Array[float] = [0.25, 0.5, 1.0]

## Center-of-mass offset standing in for the kart's, meters, from the body origin.
## `kart_debug_vehicle.gd` puts the mesh origin on the ground at the front axle
## line and the center of mass 0.23 m up and about 0.55 m back, so these are that
## geometry rounded. Used by the "kart lever" cases, which apply the same drive
## force two ways and let the difference in pitch torque speak.
const KART_COM := Vector3(0.0, 0.23, -0.55)

## A representative drive force at the contact patch, newtons. `TRACTION_LIMIT`
## from `kart_debug_vehicle.gd`.
const KART_DRIVE := 2650.0

## Force ratios for the vertical sweep, as multiples of the body's own weight.
## Dense around 1.0 because that is where a wheel spring operates: a spring
## holding the kart up is applying almost exactly its share of `m·g`, and the
## question is whether the engine's contact and the applied force fight over the
## last percent.
const VERTICAL_RATIOS: Array[float] = [0.25, 0.50, 0.90, 0.99, 1.00, 1.01, 1.10, 1.50, 2.00]

## Friction values written onto the *body* while the ground stays at 1.0. 0.0 is
## the value M3b would use if the answer to question 3 is "turn Jolt's friction
## off"; 1.0 is what a body inherits by default and therefore what the kart has
## today.
const LATERAL_FRICTIONS: Array[float] = [0.0, 0.5, 1.0]

## Lateral force ratios, as multiples of weight. Below, at, and well above a
## µ = 1 Coulomb threshold, so the threshold can be bracketed and — from the
## k = 2 case, where the body is definitely sliding — the effective coefficient
## can be solved for directly as µ = (F/m − a)/g.
const LATERAL_RATIOS: Array[float] = [0.50, 1.00, 2.00]

## Principal moments of inertia for the rotational cases, kg·m². Three distinct
## values on purpose: a diagonal tensor with two equal entries cannot tell a
## world-space lever arm from a body-local one after a rotation about the axis
## they share.
const INERTIA := Vector3(2.0, 5.0, 11.0)

## Torque-producing force for the rotational cases, newtons.
const SPIN_FORCE := 1000.0

## Speed of the raycast body, m/s. 60 m/s is 216 km/h — comfortably faster than
## the 140 km/h §6.4 asks the kart to reach, and 0.5 m of travel per tick, which
## is more than three times the wheel radius. If a ray is going to disagree with
## itself because the body moved, it will do it here.
const RAY_SPEED := 60.0

## Height the raycast body flies at, meters. Its analytic ray distance to the
## plane is exactly this.
const RAY_HEIGHT := 2.0

## Rays fired per timing sample in the cost measurement.
const RAY_COST_SAMPLES := 20000

# --- stall test ------------------------------------------------------------

## Wall-clock milliseconds burned per idle frame under `--stall`. 100 ms is
## 10 fps, which needs 12 physics ticks per frame at 120 Hz against a default
## `max_physics_steps_per_frame` of 8 — a 1.5x demand, chosen so the shortfall is
## unambiguous rather than marginal.
const STALL_BUSY_MS := 100

## How long the stall runs, in physics ticks.
const STALL_TICKS := 240


## A rigid body that records what the contact solver did to it.
##
## The contact impulses are only readable from `_integrate_forces`, which is why
## this exists at all rather than the probe using plain `RigidBody3D`s — and
## reading them is the only *direct* measurement of the normal force in this
## file. Everything else infers force from motion; this one asks the solver.
##
## `_integrate_forces` is used to **read**, never to apply. ADR-0032 conclusion 2
## measured `_physics_process` and `_integrate_forces` to be identical to the
## last digit for force application, and input arrives in `_physics_process`, so
## that is where the pushing happens.
class ProbeBody extends RigidBody3D:
	var contact_count := 0
	var contact_impulse := Vector3.ZERO
	var contact_normal := Vector3.ZERO
	var solver_step := 0.0

	func _integrate_forces(state: PhysicsDirectBodyState3D) -> void:
		contact_count = state.get_contact_count()
		contact_impulse = Vector3.ZERO
		contact_normal = Vector3.ZERO
		for index in contact_count:
			contact_impulse += state.get_contact_impulse(index)
			contact_normal += state.get_contact_local_normal(index)
		solver_step = state.step


var _cases: Array[Dictionary] = []
var _lanes := 0
var _tick := 0
var _delta := 0.0

## Gravity as the project declares it. Used to size the applied forces; the
## analytic answers are computed from the *measured* gravity instead, so that a
## project that changed this setting would still be judged against itself.
var _gravity_setting := 9.8

## Free-falling reference body. Its acceleration is the measured gravity.
var _gravity_body: RigidBody3D
var _gravity_v0 := 0.0

## The moving body the raycasts are fired from and, in one case, at.
var _ray_body: ProbeBody

## The 175 kg resting body, kept so a ray can be fired from something that is
## actually sitting on the plane rather than flying over it.
var _rest_reference: ProbeBody

var _ray_results: Dictionary = {}
var _ray_cost_us := {"excluded": 0, "included": 0}

var _lines: Array[String] = []
var _stall := false
var _stall_started := false
var _stall_tick := 0
var _stall_start_us := 0
var _stall_frames := 0


func _initialize() -> void:
	for argument in OS.get_cmdline_user_args():
		if argument == "--stall":
			_stall = true
		elif argument.begins_with("--hz="):
			# The whole probe re-run at another tick rate. Not a knob for taste:
			# several numbers below — the contact impulse read-out's 0.0371 %
			# offset in particular — are only interpretable if you know whether
			# they scale with the step, and the only way to find out is to change
			# it. M3b substeps at 240 Hz, so 240 is the rate that matters.
			Engine.physics_ticks_per_second = int(argument.substr(5))

	_gravity_setting = float(ProjectSettings.get_setting("physics/3d/default_gravity", 9.8))

	_build_ground()

	# --- question 1: resting contact, three masses an order of magnitude apart.
	# If the rest height or the residual velocity depends on mass, a wheel spring
	# calibrated on a static kart is wrong on a loaded one.
	for mass in [17.5, 175.0, 1750.0]:
		_add_case({
			"group": "rest",
			"name": "rest, %.1f kg" % mass,
			"mass": mass,
		})
	# A sphere as well as boxes. A box on a plane rests on four contact points and
	# a sphere on one, which is what a tire is much closer to; if the read-out or
	# the rest height depends on how many points the manifold has, this is where
	# it shows.
	_add_case({
		"group": "rest",
		"name": "rest, sphere",
		"mass": KART_MASS,
		"sphere": true,
	})
	# Two more resting bodies pressed *down* with a known extra force. Their
	# analytic normal is m*g + F, and comparing all five tells a multiplicative
	# error in the impulse readout from an additive one — which matters, because
	# a tire model that reads normal load from the solver is reading this number.
	for extra in [1.0, 3.0]:
		_add_case({
			"group": "rest_loaded",
			"name": "rest + %.1f mg down" % extra,
			"mass": KART_MASS,
			"ratio": extra,
		})
	# The same load with gravity switched off entirely and the whole normal force
	# supplied by `apply_central_force`. If the read-out's offset survives that,
	# it is a property of the contact solver and not of how gravity is applied.
	_add_case({
		"group": "rest_loaded",
		"name": "no gravity, 2.0 mg down",
		"mass": KART_MASS,
		"ratio": 2.0,
		"gravity_scale": 0.0,
	})

	# --- question 2: an upward force at the contact patch.
	#
	# The offset is straight down from the center of mass to the contact point,
	# and the force is straight up, so `r × F` is exactly zero: this case is a
	# pure linear measurement with no torque contaminating it. That is
	# deliberate — question 5 tests the torque path separately, and mixing them
	# would make a wrong answer here ambiguous.
	for ratio in VERTICAL_RATIOS:
		_add_case({
			"group": "vertical",
			"name": "up %.2f mg at patch" % ratio,
			"mass": KART_MASS,
			"ratio": ratio,
		})

	# --- question 3: lateral force against Jolt's own friction.
	for friction in LATERAL_FRICTIONS:
		for ratio in LATERAL_RATIOS:
			_add_case({
				"group": "lateral",
				"name": "side %.2f mg, mu_body %.2f" % [ratio, friction],
				"mass": KART_MASS,
				"ratio": ratio,
				"friction": friction,
			})
		# The same push moved down to the contact patch, where it also generates
		# a rolling torque. If the friction threshold differs between the two,
		# then "how much force moves the body" is not a property of the surface
		# and the solver cannot treat it as one.
		_add_case({
			"group": "lateral_patch",
			"name": "side 2.00 mg at patch, mu %.2f" % friction,
			"mass": KART_MASS,
			"ratio": 2.0,
			"friction": friction,
		})

	# --- question 3, second half: the combine rule.
	#
	# The same lateral push, on a slab whose friction is 0.5 instead of 1.0. Five
	# candidate rules predict five different effective coefficients here, and the
	# rule is what decides whether "friction 0 on the chassis" is a *sufficient*
	# fix or merely a fix against this one ground material.
	var slab_first_lane := _lanes
	for friction in COMBINE_FRICTIONS:
		_add_case({
			"group": "combine",
			"name": "slab mu %.2f, body mu %.2f" % [SLAB_FRICTION, friction],
			"mass": KART_MASS,
			"ratio": 2.0,
			"friction": friction,
			"surface": SLAB_TOP,
		})
	_build_slab(slab_first_lane, COMBINE_FRICTIONS.size())

	# --- question 6: ADR-0032 conclusion 3, re-asked with a contact underneath.
	#
	# Zero friction on these four so the horizontal pair slides freely and the
	# only thing the contact is resolving is the normal direction. The vertical
	# pair straddles `m·g`, so its substeps are summing across the exact
	# transition where the contact stops holding the body up.
	_add_case({"group": "sub_h1", "name": "sub: 1x 0.50mg sideways", "mass": KART_MASS,
		"ratio": 0.5, "friction": 0.0, "splits": 1})
	_add_case({"group": "sub_h4", "name": "sub: 4x 0.125mg sideways", "mass": KART_MASS,
		"ratio": 0.5, "friction": 0.0, "splits": 4})
	# 2.00 mg rather than 1.50 mg, purely so the vertical pair's answer (a = g)
	# differs from the horizontal pair's (a = g/2). At 1.50 mg the two produced
	# identical numbers — correct, and indistinguishable from a copy-paste bug.
	_add_case({"group": "sub_v1", "name": "sub: 1x 2.00mg up", "mass": KART_MASS,
		"ratio": 2.0, "friction": 0.0, "splits": 1})
	_add_case({"group": "sub_v4", "name": "sub: 4x 0.500mg up", "mass": KART_MASS,
		"ratio": 2.0, "friction": 0.0, "splits": 4})

	# --- question 5: offset force and a custom inertia tensor.
	#
	# In free space at altitude, gravity off, so nothing but `apply_force` and
	# the inertia tensor is under test. Each is pushed for exactly one tick and
	# read on the next: from rest, `ω = I⁻¹(r × F)·dt` is exact, with no `ω × Iω`
	# gyroscopic term to model. Let them run longer and the analytic answer stops
	# being closed-form, which is how a probe starts measuring its own model.
	_add_case({
		"group": "spin", "name": "spin: axis-aligned", "mass": KART_MASS,
		"altitude": 60.0, "inertia": INERTIA,
		"offset": Vector3(0.0, 0.0, 1.0), "force": Vector3(SPIN_FORCE, 0.0, 0.0),
		"yaw45": false, "com": Vector3.ZERO,
	})
	_add_case({
		# Rotated 45 degrees about Z, which makes the *world* inertia tensor
		# non-diagonal — `RigidBody3D.inertia` is a Vector3 and can only express
		# principal moments, so this is the only way to ask the non-diagonal
		# question at all. It doubles as the test of whether `apply_force`'s
		# offset is a world vector or a body-local one: the two interpretations
		# predict different axes here, and both are printed.
		"group": "spin", "name": "spin: body rolled 45 deg", "mass": KART_MASS,
		"altitude": 80.0, "inertia": INERTIA,
		"offset": Vector3(0.0, 1.0, 0.0), "force": Vector3(0.0, 0.0, SPIN_FORCE),
		"yaw45": true, "com": Vector3.ZERO,
	})
	_add_case({
		# The decisive one-bit test of CLAUDE.md's standing trap. The center of
		# mass is moved half a meter up from the origin and the force is applied
		# at offset **zero**. If the offset is measured from the center of mass
		# the torque is zero and the body never rotates; if it is measured from
		# the body origin the lever arm is (origin − com) and the body spins
		# about +Z at a rate this file predicts exactly.
		"group": "spin", "name": "spin: offset zero, com +0.5y", "mass": KART_MASS,
		"altitude": 100.0, "inertia": INERTIA,
		"offset": Vector3.ZERO, "force": Vector3(SPIN_FORCE, 0.0, 0.0),
		"yaw45": false, "com": Vector3(0.0, 0.5, 0.0),
	})

	# --- question 5, applied: the lever arm the kart actually uses.
	#
	# Two identical bodies with the kart's center-of-mass offset, each given the
	# same drive force at the same contact patch, differing only in what offset
	# is handed to `apply_force`:
	#
	#   "as kart_debug_vehicle" passes `contact_world - com_world`, which is what
	#   `_force_at_contact` computes today on the documented-in-CLAUDE.md belief
	#   that the offset is measured from the center of mass;
	#   "from body origin" passes `contact_world - origin_world`.
	#
	# Whichever of the two produces the physically intended pitch torque of
	# (contact - com) x F is the one the solver must use. They cannot both.
	for style in ["as kart_debug_vehicle", "from body origin"]:
		_add_case({
			"group": "kartlever",
			"name": "lever, %s" % style,
			"mass": KART_MASS,
			"altitude": 120.0 if style == "from body origin" else 140.0,
			"inertia": INERTIA,
			"com": KART_COM,
			"style": style,
			# Contact patch, in body-origin-relative world coordinates: on the
			# ground under the rear axle. The kart's origin is on the ground, so
			# the patch is level with it and 1.1 m behind.
			"patch": Vector3(0.0, 0.0, -1.1),
			"force": Vector3(0.0, 0.0, -KART_DRIVE),
		})

	_build_ray_body()
	_build_gravity_body()
	_check_layout()


## Every lane must sit over the ground, and every sliding body must stay over it
## for the whole run. Checked rather than eyeballed, because the failure is a
## table of plausible-looking misses rather than a crash.
func _check_layout() -> void:
	var last_lane := float(_lanes - 1) * LANE_SPACING
	var half := GROUND_SIZE * 0.5
	if last_lane + LANE_SPACING > half:
		push_error(
			"lane %d sits at x = %.1f m, past the ground's %.1f m half-extent; " % [
				_lanes - 1, last_lane, half,
			] + "raise GROUND_SIZE"
		)
	# The fastest sliding body travels a*t^2/2 with a = 2 g, and the raycast body
	# covers RAY_SPEED * the whole measured window from where it starts.
	var seconds := float(TICKS_MEASURE) / float(Engine.physics_ticks_per_second)
	var farthest_slide := maxf(
		_gravity_setting * seconds * seconds, RAY_SPEED * seconds
	)
	if farthest_slide > half:
		push_error("a sliding body travels %.1f m, past the ground edge" % farthest_slide)


## The lower-friction slab under the `combine` cases. Static, so it cannot move
## and cannot contribute anything but its material.
func _build_slab(first_lane: int, lane_count: int) -> void:
	var body := StaticBody3D.new()
	body.name = "Slab"
	var material := PhysicsMaterial.new()
	material.friction = SLAB_FRICTION
	body.physics_material_override = material

	var shape := BoxShape3D.new()
	# Long in Z because the bodies on it slide that way — a body that runs off
	# the end stops being a friction measurement without saying so.
	shape.size = Vector3(float(lane_count) * LANE_SPACING, GROUND_THICKNESS, 140.0)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	collider.position = Vector3(
		(float(first_lane) + float(lane_count - 1) * 0.5) * LANE_SPACING,
		SLAB_TOP - GROUND_THICKNESS * 0.5,
		40.0
	)
	body.add_child(collider)
	get_root().add_child(body)


# --- construction ----------------------------------------------------------


func _build_ground() -> void:
	var body := StaticBody3D.new()
	body.name = "Ground"
	var material := PhysicsMaterial.new()
	material.friction = GROUND_FRICTION
	# `rough` and `absorbent` are GodotPhysics3D hints with no Jolt equivalent.
	# Left at their defaults here rather than copied from the proving ground, so
	# that nothing in this file depends on a flag the running engine ignores.
	body.physics_material_override = material

	var shape := BoxShape3D.new()
	shape.size = Vector3(GROUND_SIZE, GROUND_THICKNESS, GROUND_SIZE)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	# Sunk so the top face is exactly y = 0. Every analytic rest height in this
	# file is written against that plane, so a half-thickness error here would
	# shift all of them by 1 m at once — which is at least loud.
	collider.position = Vector3(0.0, -GROUND_THICKNESS * 0.5, 0.0)
	body.add_child(collider)
	get_root().add_child(body)


func _add_case(spec: Dictionary) -> void:
	var body := ProbeBody.new()
	body.name = "Case%d" % _lanes
	body.mass = float(spec["mass"])
	# Everything that could add a force other than the one under test, off.
	# `linear_damp` defaults to 0.1 project-wide — 1 m/s² at 10 m/s, a quarter of
	# the kart's acceleration there, arriving from nowhere. ADR-0031.
	body.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	body.linear_damp = 0.0
	body.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	body.angular_damp = 0.0
	# A body asleep is a body whose contact is not being solved, and the resting
	# cases would otherwise measure Jolt's sleep threshold instead of its
	# contact. `kart_debug_vehicle.gd` disables sleeping for the same reason.
	body.can_sleep = false
	body.contact_monitor = true
	body.max_contacts_reported = 8

	var altitude: float = spec.get("altitude", 0.0)
	var free_space: bool = altitude > 0.0
	body.gravity_scale = 0.0 if free_space else float(spec.get("gravity_scale", 1.0))

	if spec.has("friction"):
		var material := PhysicsMaterial.new()
		material.friction = float(spec["friction"])
		body.physics_material_override = material

	if spec.has("inertia"):
		body.inertia = spec["inertia"]
	var com: Vector3 = spec.get("com", Vector3.ZERO)
	if com != Vector3.ZERO:
		body.center_of_mass_mode = RigidBody3D.CENTER_OF_MASS_MODE_CUSTOM
		body.center_of_mass = com

	var shape := BoxShape3D.new()
	shape.size = Vector3.ONE * (HALF_EXTENT * 2.0)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	body.add_child(collider)

	# Spawned exactly at the analytic rest height rather than dropped onto it.
	# A drop would measure the landing; this measures whether the engine holds a
	# body where geometry says it belongs.
	var surface: float = spec.get("surface", 0.0)
	var height: float = altitude if free_space else surface + HALF_EXTENT
	body.position = Vector3(float(_lanes) * LANE_SPACING, height, 0.0)
	if spec.get("yaw45", false):
		body.rotation = Vector3(0.0, 0.0, PI / 4.0)

	get_root().add_child(body)
	_lanes += 1

	if String(spec["group"]) == "rest" and _rest_reference == null and is_equal_approx(
		body.mass, KART_MASS
	):
		_rest_reference = body

	spec["body"] = body
	spec["rest_y_min"] = INF
	spec["rest_y_max"] = -INF
	spec["rest_y_sum"] = 0.0
	spec["rest_speed_max"] = 0.0
	spec["rest_samples"] = 0
	spec["rest_normal_force"] = 0.0
	spec["rest_contacts"] = 0
	spec["push_normal_force"] = 0.0
	spec["push_normal_min"] = INF
	spec["push_normal_max"] = -INF
	spec["push_tangent_force"] = 0.0
	spec["push_samples"] = 0
	spec["push_lag_force"] = 0.0
	spec["left_ground_tick"] = -1
	spec["omega_one_tick"] = Vector3.ZERO
	spec["dv_one_tick"] = Vector3.ZERO
	_cases.append(spec)


func _build_ray_body() -> void:
	# Flies at constant height with gravity off, so its analytic ray distance to
	# the plane is `RAY_HEIGHT` on every tick of the run and any deviation is the
	# raycast's, not the body's.
	_ray_body = ProbeBody.new()
	_ray_body.name = "RayBody"
	_ray_body.mass = KART_MASS
	_ray_body.gravity_scale = 0.0
	_ray_body.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	_ray_body.linear_damp = 0.0
	_ray_body.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	_ray_body.angular_damp = 0.0
	_ray_body.can_sleep = false
	var shape := BoxShape3D.new()
	shape.size = Vector3.ONE * (HALF_EXTENT * 2.0)
	var collider := CollisionShape3D.new()
	collider.shape = shape
	_ray_body.add_child(collider)
	_ray_body.position = Vector3(float(_lanes) * LANE_SPACING, RAY_HEIGHT, -80.0)
	get_root().add_child(_ray_body)
	_lanes += 1


func _build_gravity_body() -> void:
	# Gravity is measured rather than read, because every analytic answer in this
	# file is a multiple of it. A body with no collision shape cannot contact
	# anything, so this is free fall and nothing else.
	_gravity_body = RigidBody3D.new()
	_gravity_body.name = "GravityReference"
	_gravity_body.mass = 1.0
	_gravity_body.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	_gravity_body.linear_damp = 0.0
	_gravity_body.can_sleep = false
	_gravity_body.position = Vector3(float(_lanes) * LANE_SPACING, 400.0, 0.0)
	get_root().add_child(_gravity_body)
	_lanes += 1


# --- the run ---------------------------------------------------------------


func _physics_process(delta: float) -> bool:
	_delta = delta

	if _stall_started:
		return _stall_step()

	if _tick < TICKS_SETTLE:
		if _tick >= TICKS_SETTLE - TICKS_REST_WINDOW:
			_sample_rest()
		if _tick == TICKS_SETTLE - 1:
			_ray_body.linear_velocity = Vector3(0.0, 0.0, RAY_SPEED)
		_tick += 1
		return false

	var measure_tick := _tick - TICKS_SETTLE

	if measure_tick == 0:
		_snapshot_baselines()

	if measure_tick == 1:
		_record_one_tick_response()

	if measure_tick < TICKS_MEASURE:
		_apply_forces(measure_tick)
		_sample_push(measure_tick)
		_sample_rays()
		_tick += 1
		return false

	_measure_ray_cost()
	_report()

	if _stall:
		_stall_started = true
		_stall_start_us = Time.get_ticks_usec()
		return false

	quit(0)
	return true


func _sample_rest() -> void:
	for spec in _cases:
		var body: ProbeBody = spec["body"]
		if spec.get("altitude", 0.0) > 0.0:
			continue
		var y := body.global_position.y
		spec["rest_y_min"] = minf(float(spec["rest_y_min"]), y)
		spec["rest_y_max"] = maxf(float(spec["rest_y_max"]), y)
		spec["rest_y_sum"] = float(spec["rest_y_sum"]) + y
		spec["rest_speed_max"] = maxf(float(spec["rest_speed_max"]), body.linear_velocity.length())
		spec["rest_samples"] = int(spec["rest_samples"]) + 1
		spec["rest_normal_force"] = float(spec["rest_normal_force"]) + body.contact_impulse.y / _delta
		spec["rest_contacts"] = int(spec["rest_contacts"]) + body.contact_count


func _snapshot_baselines() -> void:
	for spec in _cases:
		var body: ProbeBody = spec["body"]
		spec["y0"] = body.global_position.y
		spec["p0"] = body.global_position
		spec["v0"] = body.linear_velocity
		spec["w0"] = body.angular_velocity
		# The orientation at the instant the force is applied, not at report
		# time. The rotational cases are still spinning when the report runs, and
		# predicting their one-tick response from a basis that has since moved
		# was worth an 0.019 rad/s "error" that was entirely the probe's own.
		spec["basis0"] = body.global_transform.basis
	_gravity_v0 = _gravity_body.linear_velocity.y


func _record_one_tick_response() -> void:
	# Exactly one physics step has elapsed since the single force application in
	# the "spin" cases, so `ω = α·dt` and `Δv = (F/m)·dt` are exact.
	for spec in _cases:
		var group := String(spec["group"])
		if group != "spin" and group != "kartlever":
			continue
		var body: ProbeBody = spec["body"]
		spec["omega_one_tick"] = body.angular_velocity - Vector3(spec["w0"])
		spec["dv_one_tick"] = body.linear_velocity - Vector3(spec["v0"])


func _apply_forces(measure_tick: int) -> void:
	for spec in _cases:
		var body: ProbeBody = spec["body"]
		var weight: float = float(spec["mass"]) * _gravity_setting
		# Straight down from the body to the contact patch, in world coordinates.
		# These bodies leave `center_of_mass` at its default, which for a centered
		# box shape *is* the body origin, so the origin-versus-center-of-mass
		# ambiguity question 5 settles cannot contaminate questions 2, 3 or 6 —
		# both readings of the offset name the same point. The cases that do care
		# are the ones in question 5, which move the center of mass on purpose.
		var patch := (
			body.global_position + Vector3.DOWN * HALF_EXTENT
			- body.to_global(body.center_of_mass)
		)

		match String(spec["group"]):
			"vertical":
				body.apply_force(Vector3.UP * float(spec["ratio"]) * weight, patch)
			"rest_loaded":
				body.apply_central_force(Vector3.DOWN * float(spec["ratio"]) * weight)
			"lateral", "combine":
				body.apply_central_force(Vector3(0.0, 0.0, float(spec["ratio"]) * weight))
			"lateral_patch":
				body.apply_force(Vector3(0.0, 0.0, float(spec["ratio"]) * weight), patch)
			"sub_h1", "sub_h4":
				var splits: int = int(spec["splits"])
				var each := Vector3(0.0, 0.0, float(spec["ratio"]) * weight / float(splits))
				for _i in splits:
					body.apply_central_force(each)
			"sub_v1", "sub_v4":
				var vsplits: int = int(spec["splits"])
				var veach := Vector3.UP * float(spec["ratio"]) * weight / float(vsplits)
				for _i in vsplits:
					body.apply_force(veach, patch)
			"spin":
				if measure_tick == 0:
					body.apply_force(Vector3(spec["force"]), Vector3(spec["offset"]))
			"kartlever":
				if measure_tick == 0:
					var contact := body.global_position + Vector3(spec["patch"])
					var offset := contact - body.global_position
					if String(spec["style"]) == "as kart_debug_vehicle":
						offset = contact - body.to_global(body.center_of_mass)
					body.apply_force(Vector3(spec["force"]), offset)


func _sample_push(measure_tick: int) -> void:
	for spec in _cases:
		var body: ProbeBody = spec["body"]
		var group := String(spec["group"])
		if group == "spin" or group == "rest" or group == "kartlever":
			continue
		# The tick a body first rises measurably above its own settled height.
		# For a unilateral contact under more than its own weight this should be
		# the first or second tick; anything later is the contact constraint
		# holding on to force that the free-body answer says has already left.
		if int(spec["left_ground_tick"]) < 0:
			if body.global_position.y > float(spec["y0"]) + 1e-4:
				spec["left_ground_tick"] = measure_tick
		# The impulse read-out lags the applied force by exactly one tick: on the
		# first tick of the push every case still reports its undisturbed resting
		# normal of m*g, and averaging that stale sample into the mean produced a
		# spurious 7.15 N of "contact" on cases that had already left the ground
		# — 1715.63 / 240, which is how the lag was found. It is recorded on its
		# own rather than discarded, because a solver reading contact load from
		# this API is reading last tick's answer.
		if measure_tick == 0:
			spec["push_lag_force"] = body.contact_impulse.y / _delta
			continue
		if body.contact_count > 0:
			var normal := body.contact_impulse.y / _delta
			spec["push_normal_force"] = float(spec["push_normal_force"]) + normal
			spec["push_normal_min"] = minf(float(spec["push_normal_min"]), normal)
			spec["push_normal_max"] = maxf(float(spec["push_normal_max"]), normal)
			spec["push_tangent_force"] = float(spec["push_tangent_force"]) + body.contact_impulse.z / _delta
			spec["push_samples"] = int(spec["push_samples"]) + 1


# --- raycasts --------------------------------------------------------------


func _sample_rays() -> void:
	var space := get_root().get_world_3d().direct_space_state
	var origin := _ray_body.global_position

	# 1. Straight down from the body center, its own collider excluded. The
	#    analytic distance is exactly the flight height.
	var down := PhysicsRayQueryParameters3D.create(origin, origin + Vector3.DOWN * 10.0)
	down.exclude = [_ray_body.get_rid()]
	_record_ray("down, excluded", space.intersect_ray(down), RAY_HEIGHT, Vector3.UP, origin)

	# 2. The identical ray with nothing excluded. The origin is *inside* the
	#    body's own box, so this asks whether Jolt reports a hit on a shape the
	#    ray starts within — the difference between a suspension ray that finds
	#    the road and one that finds its own wheel.
	var down_all := PhysicsRayQueryParameters3D.create(origin, origin + Vector3.DOWN * 10.0)
	_record_ray("down, not excluded", space.intersect_ray(down_all), RAY_HEIGHT, Vector3.UP, origin)

	# 3. A tilted ray. An axis-aligned ray onto an axis-aligned box can be right
	#    for reasons that do not generalize; this one has an analytic length of
	#    height / |dir.y| and an analytic landing point offset sideways.
	var direction := Vector3(0.6, -0.8, 0.0)
	var tilted := PhysicsRayQueryParameters3D.create(origin, origin + direction * 12.5)
	tilted.exclude = [_ray_body.get_rid()]
	_record_ray("tilted 0.6/-0.8", space.intersect_ray(tilted), RAY_HEIGHT / 0.8, Vector3.UP, origin)

	# 4. From a point 1 m above the body's center, pointing down, with nothing
	#    excluded. Unlike case 2 the origin is now *outside* the body, so the ray
	#    can see it: this is the wheel-mount case, a ray cast from above the
	#    chassis toward the road. Analytic distance to the box's top face is
	#    1 m minus the half-extent.
	var above := origin + Vector3.UP
	var from_above := PhysicsRayQueryParameters3D.create(above, above + Vector3.DOWN * 10.0)
	_record_ray(
		"from above, not excl", space.intersect_ray(from_above),
		1.0 - HALF_EXTENT, Vector3.UP, above
	)

	# 5. The same ray with the body excluded, which is what it takes to reach the
	#    road. Analytic distance is the flight height plus the 1 m head start.
	var from_above_excl := PhysicsRayQueryParameters3D.create(above, above + Vector3.DOWN * 10.0)
	from_above_excl.exclude = [_ray_body.get_rid()]
	_record_ray(
		"from above, excluded", space.intersect_ray(from_above_excl),
		RAY_HEIGHT + 1.0, Vector3.UP, above
	)

	# 6. The same query from a body that is *resting* on the plane rather than
	#    flying above it, which is the case a suspension ray is in for all but a
	#    few percent of a lap. Analytic distance is the half-extent exactly, so
	#    any steady-state penetration shows up here as a shortened ray — which is
	#    precisely how it would corrupt a spring length.
	if _rest_reference != null:
		var resting := _rest_reference.global_position
		var rest_ray := PhysicsRayQueryParameters3D.create(resting, resting + Vector3.DOWN * 2.0)
		rest_ray.exclude = [_rest_reference.get_rid()]
		_record_ray("from resting body", space.intersect_ray(rest_ray), HALF_EXTENT, Vector3.UP, resting)

	# 7. A ray that starts *below* the surface, pointing down. This is a wheel
	#    buried in a curb: the suspension ray's origin has ended up inside the
	#    geometry it is supposed to measure against. A miss here means a spring
	#    that reads "no contact" at the exact moment the wheel is deepest into
	#    the obstacle, and a kart that falls through it.
	var buried := Vector3(origin.x, -0.5, origin.z)
	var inside_down := PhysicsRayQueryParameters3D.create(buried, buried + Vector3.DOWN * 2.0)
	_record_ray("from inside, down", space.intersect_ray(inside_down), NAN, Vector3.ZERO, buried)

	# 8. The same origin, pointing up and out. A spring that recovers from
	#    penetration needs to know how deep it is, and this is the query that
	#    would tell it.
	var inside_up := PhysicsRayQueryParameters3D.create(buried, buried + Vector3.UP * 2.0)
	_record_ray("from inside, up", space.intersect_ray(inside_up), NAN, Vector3.ZERO, buried)

	# 9. The same query with `hit_from_inside` on, which is the documented switch
	#    for exactly this case and which nothing in this project uses yet.
	var inside_flag := PhysicsRayQueryParameters3D.create(buried, buried + Vector3.UP * 2.0)
	inside_flag.hit_from_inside = true
	_record_ray("from inside, up, inside", space.intersect_ray(inside_flag), NAN, Vector3.ZERO, buried)

	# 10. And with `hit_back_faces` instead, the other switch that sounds like it
	#     should apply. Both are tried because guessing which one a buried
	#     suspension ray needs is exactly the kind of assumption this file exists
	#     to replace.
	var inside_back := PhysicsRayQueryParameters3D.create(buried, buried + Vector3.UP * 2.0)
	inside_back.hit_back_faces = true
	_record_ray(
		"from inside, up, backface", space.intersect_ray(inside_back),
		NAN, Vector3.ZERO, buried
	)


func _record_ray(
	key: String, result: Dictionary, analytic_distance: float, analytic_normal: Vector3, from: Vector3
) -> void:
	if not _ray_results.has(key):
		_ray_results[key] = {
			"hits": 0, "misses": 0,
			"max_distance_error": 0.0, "max_normal_error": 0.0,
			"last_distance": NAN, "last_normal": Vector3.ZERO,
			"analytic_distance": analytic_distance, "analytic_normal": analytic_normal,
			"self_hits": 0,
		}
	var entry: Dictionary = _ray_results[key]
	if result.is_empty():
		entry["misses"] = int(entry["misses"]) + 1
		return
	entry["hits"] = int(entry["hits"]) + 1
	var distance: float = (Vector3(result["position"]) - from).length()
	var normal: Vector3 = result["normal"]
	entry["last_distance"] = distance
	entry["last_normal"] = normal
	if int(result["rid"].get_id()) == int(_ray_body.get_rid().get_id()):
		entry["self_hits"] = int(entry["self_hits"]) + 1
	if not is_nan(analytic_distance):
		entry["max_distance_error"] = maxf(
			float(entry["max_distance_error"]), absf(distance - analytic_distance)
		)
		entry["max_normal_error"] = maxf(
			float(entry["max_normal_error"]), (normal - analytic_normal).length()
		)


func _measure_ray_cost() -> void:
	# The one wall-clock number in the deterministic half of this file. It gates
	# nothing, it is not compared against a threshold, and it will differ between
	# machines and between runs — it is here to answer "what does `exclude`
	# cost", which has no analytic answer and cannot be measured any other way.
	var space := get_root().get_world_3d().direct_space_state
	var origin := _ray_body.global_position

	var with_exclude := PhysicsRayQueryParameters3D.create(origin, origin + Vector3.DOWN * 10.0)
	with_exclude.exclude = [_ray_body.get_rid()]
	var without := PhysicsRayQueryParameters3D.create(origin, origin + Vector3.DOWN * 10.0)

	# Each loop runs three times and the *fastest* pass is reported, which is the
	# standard way to read a microbenchmark: the slow passes are contaminated by
	# scheduling and the fast one is not. The first version of this measurement
	# timed one pass each and had `exclude` looking 24 % cheaper on one run and
	# 86 % dearer on the next, entirely because whichever loop ran first paid for
	# warming the broadphase.
	_ray_cost_us["excluded"] = 1 << 30
	_ray_cost_us["included"] = 1 << 30
	for _pass in 3:
		var started := Time.get_ticks_usec()
		for _i in RAY_COST_SAMPLES:
			space.intersect_ray(with_exclude)
		_ray_cost_us["excluded"] = mini(
			_ray_cost_us["excluded"], Time.get_ticks_usec() - started
		)

		started = Time.get_ticks_usec()
		for _i in RAY_COST_SAMPLES:
			space.intersect_ray(without)
		_ray_cost_us["included"] = mini(
			_ray_cost_us["included"], Time.get_ticks_usec() - started
		)


# --- question 7: an induced stall ------------------------------------------


func _process(_delta: float) -> bool:
	if not _stall_started:
		return false
	# Burn wall-clock time in the *idle* frame, which is what a frame-rate
	# collapse actually is. Godot then owes the physics loop more ticks than
	# `max_physics_steps_per_frame` permits it to run, and the shortfall is the
	# measurement.
	var until := Time.get_ticks_usec() + STALL_BUSY_MS * 1000
	while Time.get_ticks_usec() < until:
		pass
	_stall_frames += 1
	return false


func _stall_step() -> bool:
	_stall_tick += 1
	if _stall_tick < STALL_TICKS:
		return false

	var elapsed_us := Time.get_ticks_usec() - _stall_start_us
	var elapsed := float(elapsed_us) / 1_000_000.0
	var simulated := float(_stall_tick) * _delta
	var owed := elapsed * float(Engine.physics_ticks_per_second)

	var out: Array[String] = []
	out.append("")
	out.append("=== 7. max_physics_steps_per_frame, under an induced stall =============")
	out.append("")
	out.append("    This section reads wall-clock time by necessity: it measures the")
	out.append("    relationship between wall-clock and simulation time, and there is no")
	out.append("    deterministic way to ask that. It runs only under --stall.")
	out.append("")
	out.append("    max_physics_steps_per_frame  %d" % Engine.max_physics_steps_per_frame)
	out.append("    physics_jitter_fix           %.4f" % Engine.physics_jitter_fix)
	out.append("    busy work per idle frame     %d ms" % STALL_BUSY_MS)
	out.append("")
	# The analytic model, stated before the numbers so it can be judged against
	# them: a frame that takes T wall-seconds owes the physics loop hz*T ticks
	# and is permitted max_physics_steps_per_frame, so it runs
	# min(hz*T, max_steps) and simulation time advances at
	# min(hz*T, max_steps) / (hz*T) of wall-clock. Nothing catches the shortfall
	# up later — the accumulator is clamped, not banked.
	var frames := maxf(float(_stall_frames), 1.0)
	var frame_seconds := elapsed / frames
	var owed_per_frame := frame_seconds * float(Engine.physics_ticks_per_second)
	var analytic_per_frame := minf(owed_per_frame, float(Engine.max_physics_steps_per_frame))
	var analytic_ratio := analytic_per_frame / owed_per_frame

	out.append("    idle frames rendered         %d" % _stall_frames)
	out.append("    wall-clock per idle frame    %.4f s" % frame_seconds)
	out.append("    physics ticks executed       %d" % _stall_tick)
	out.append("    ticks wall-clock owed        %.1f" % owed)
	out.append("    simulated time               %.4f s" % simulated)
	out.append("    wall-clock time              %.4f s" % elapsed)
	out.append("")
	out.append("    %-28s %12s %12s" % ["", "measured", "analytic"])
	out.append("    %-28s %12.4f %12.4f" % [
		"ticks per idle frame", float(_stall_tick) / frames, analytic_per_frame,
	])
	out.append("    %-28s %12.4f %12.4f" % [
		"simulation / wall-clock", simulated / maxf(elapsed, 1e-9), analytic_ratio,
	])
	print("\n".join(out))
	quit(0)
	return true


# --- report ----------------------------------------------------------------


func _report() -> void:
	var g := (_gravity_body.linear_velocity.y - _gravity_v0) / (float(TICKS_MEASURE) * _delta)
	g = -g

	_environment(g)
	_report_rest(g)
	_report_vertical(g)
	_report_lateral(g)
	_report_rays()
	_report_spin()
	_report_substep()

	if not _stall:
		_lines.append("")
		_lines.append("=== 7. max_physics_steps_per_frame ====================================")
		_lines.append("")
		_lines.append("    Not measured in this run. It is the one question here whose answer")
		_lines.append("    is a relationship between wall-clock and simulation time, so it")
		_lines.append("    cannot be asked without reading a clock, and reading a clock would")
		_lines.append("    make every run above differ from the last. Pass --stall to run it.")

	print("\n".join(_lines))


func _environment(g: float) -> void:
	# A fresh space's default solver iteration count is 8 under Jolt and 16 under
	# GodotPhysics3D — the behavioral assertion from ADR-0030. Without it, every
	# number below could be a measurement of the wrong engine, which is exactly
	# what happened to every physics claim made before M3a.
	var space := PhysicsServer3D.space_create()
	var iterations := PhysicsServer3D.space_get_param(
		space, PhysicsServer3D.SPACE_PARAM_SOLVER_ITERATIONS
	)
	PhysicsServer3D.free_rid(space)

	_lines.append("=== environment =======================================================")
	_lines.append("")
	_lines.append("    engine                       %s" % Engine.get_version_info()["string"])
	_lines.append("    physics_engine setting       %s" % ProjectSettings.get_setting(
		"physics/3d/physics_engine", "(unset)"
	))
	_lines.append("    fresh space solver iters     %d   (Jolt 8, GodotPhysics3D 16)" % iterations)
	_lines.append("    physics_ticks_per_second     %d" % Engine.physics_ticks_per_second)
	_lines.append("    physics delta                %.9f s" % _delta)
	# The step the solver itself reports, as distinct from the one Godot hands
	# `_physics_process`. If the two ever differ, every force-to-impulse
	# conversion in this file and in the vehicle is scaled by the ratio.
	_lines.append("    PhysicsDirectBodyState3D.step %.9f s   residual %+.9f" % [
		_ray_body.solver_step, _ray_body.solver_step - _delta,
	])
	_lines.append("    gravity, project setting     %.6f m/s^2" % _gravity_setting)
	_lines.append("    gravity, measured free-fall  %.6f m/s^2   residual %+.9f" % [
		g, g - _gravity_setting
	])
	_lines.append("")
	_lines.append("    Every physics/* project setting in force, so that a future re-run")
	_lines.append("    against a different engine build can tell a changed default from a")
	_lines.append("    changed behavior:")
	_lines.append("")
	for property in ProjectSettings.get_property_list():
		var setting_name := String(property["name"])
		if not setting_name.begins_with("physics/"):
			continue
		if setting_name.begins_with("physics/2d"):
			continue
		_lines.append("      %-58s %s" % [setting_name, ProjectSettings.get_setting(setting_name)])


func _report_rest(g: float) -> void:
	_lines.append("")
	_lines.append("=== 1. resting contact ================================================")
	_lines.append("")
	_lines.append("    A 0.5 m box spawned exactly at its analytic rest height of")
	_lines.append("    %.4f m and left alone. A sphere too, because a box rests on four" % HALF_EXTENT)
	_lines.append("    contact points and a tire is much closer to one.")
	_lines.append("    Settled for %d ticks, sampled over the last %d." % [
		TICKS_SETTLE, TICKS_REST_WINDOW
	])
	_lines.append("    penetration is (analytic rest y - mean measured y): positive means the")
	_lines.append("    body sits lower than geometry says it should, which is the number a")
	_lines.append("    wheel spring reading a raycast distance would silently absorb.")
	_lines.append("")
	_lines.append("    %-18s %12s %12s %12s %12s %12s" % [
		"case", "mean y", "penetration", "y spread", "max |v|", "contacts",
	])
	for spec in _cases:
		if String(spec["group"]) != "rest":
			continue
		var samples := float(spec["rest_samples"])
		var mean_y := float(spec["rest_y_sum"]) / samples
		_lines.append("    %-18s %12.8f %12.8f %12.9f %12.9f %12.2f" % [
			spec["name"], mean_y, HALF_EXTENT - mean_y,
			float(spec["rest_y_max"]) - float(spec["rest_y_min"]),
			spec["rest_speed_max"], float(spec["rest_contacts"]) / samples,
		])

	_lines.append("")
	_lines.append("    Normal force, read from the solver's own contact impulses rather than")
	_lines.append("    inferred from motion. A body at rest must be receiving exactly m*g.")
	_lines.append("")
	_lines.append("    %-18s %14s %14s %12s" % ["case", "measured N", "analytic m*g", "ratio"])
	for spec in _cases:
		if String(spec["group"]) != "rest":
			continue
		var samples := float(spec["rest_samples"])
		var normal := float(spec["rest_normal_force"]) / samples
		var analytic := float(spec["mass"]) * g
		_lines.append("    %-18s %14.4f %14.4f %12.6f" % [
			spec["name"], normal, analytic, normal / analytic,
		])

	_lines.append("")
	_lines.append("    The same read-out with a known extra load pressed straight down, so")
	_lines.append("    that a multiplicative error in the impulse read-out can be told from")
	_lines.append("    an additive one. Analytic is (gravity_scale + k) * m * g.")
	_lines.append("")
	_lines.append("    %-24s %13s %13s %11s %13s %13s" % [
		"case", "measured N", "analytic N", "ratio", "tick min", "tick max",
	])
	for spec in _cases:
		if String(spec["group"]) != "rest_loaded":
			continue
		var samples := maxf(float(spec["push_samples"]), 1.0)
		var normal := float(spec["push_normal_force"]) / samples
		var analytic := (
			float(spec.get("gravity_scale", 1.0)) + float(spec["ratio"])
		) * float(spec["mass"]) * g
		_lines.append("    %-24s %13.4f %13.4f %11.6f %13.4f %13.4f" % [
			spec["name"], normal, analytic, normal / analytic,
			spec["push_normal_min"], spec["push_normal_max"],
		])


func _report_vertical(g: float) -> void:
	_lines.append("")
	_lines.append("=== 2. an upward force at a contact Jolt is resolving ==================")
	_lines.append("")
	_lines.append("    %.1f kg on the plane, pushed up at the contact patch with" % KART_MASS)
	_lines.append("    k*m*g for %d ticks (%.3f s). Two candidate analytic answers:" % [
		TICKS_MEASURE, float(TICKS_MEASURE) * _delta,
	])
	_lines.append("")
	_lines.append("      free body        a = (F - m*g)/m           = (k - 1) * g")
	_lines.append("      unilateral       a = max(0, (F - m*g)/m)   = max(0, (k-1)) * g")
	_lines.append("")
	_lines.append("    They differ for every k below 1, which is the whole operating range of")
	_lines.append("    a wheel spring holding the kart up.")
	_lines.append("")
	_lines.append("    %-22s %10s %10s %10s %10s %10s %8s" % [
		"case", "a free", "a unilat", "a meas", "dy meas", "dy unilat", "off@tick",
	])
	var elapsed := float(TICKS_MEASURE) * _delta
	for spec in _cases:
		if String(spec["group"]) != "vertical":
			continue
		var body: ProbeBody = spec["body"]
		var ratio: float = float(spec["ratio"])
		var a_free := (ratio - 1.0) * g
		var a_unilateral := maxf(0.0, a_free)
		var a_measured := (body.linear_velocity.y - Vector3(spec["v0"]).y) / elapsed
		var dy := body.global_position.y - float(spec["y0"])
		# Symplectic Euler, per ADR-0032 conclusion 1: position advances with the
		# new velocity, so N steps of constant a give a*dt^2*N(N+1)/2, not
		# 0.5*a*t^2. Predicting with the continuous form here would show a
		# spurious 8 mm error at k = 2 and invite a hunt for it.
		var dy_unilateral := a_unilateral * _delta * _delta * float(
			TICKS_MEASURE * (TICKS_MEASURE + 1)
		) / 2.0
		_lines.append("    %-22s %10.5f %10.5f %10.5f %10.5f %10.5f %8d" % [
			spec["name"], a_free, a_unilateral, a_measured, dy, dy_unilateral,
			spec["left_ground_tick"],
		])

	_lines.append("")
	_lines.append("    What the contact contributed while it lasted, from the solver's own")
	_lines.append("    impulses. For a body held on the ground the analytic normal force is")
	_lines.append("    m*g - F = (1 - k) * m * g; once k > 1 there should be no contact left.")
	_lines.append("")
	# The first tick of the push is excluded from every column but the last.
	_lines.append("    %-22s %13s %13s %12s %12s %6s %11s" % [
		"case", "measured N", "analytic N", "tick min", "tick max", "ticks", "stale t0",
	])
	for spec in _cases:
		if String(spec["group"]) != "vertical":
			continue
		var samples := maxf(float(spec["push_samples"]), 1.0)
		var normal := float(spec["push_normal_force"]) / samples
		var analytic := maxf(0.0, (1.0 - float(spec["ratio"])) * float(spec["mass"]) * g)
		_lines.append("    %-22s %13.4f %13.4f %12.4f %12.4f %6d %11.4f" % [
			spec["name"], normal, analytic,
			spec["push_normal_min"], spec["push_normal_max"], spec["push_samples"],
			spec["push_lag_force"],
		])


func _report_lateral(g: float) -> void:
	_lines.append("")
	_lines.append("=== 3. Jolt's own friction versus an applied lateral force =============")
	_lines.append("")
	_lines.append("    Ground friction is %.2f, matching proving_ground.gd." % GROUND_FRICTION)
	_lines.append("    The body's own physics_material_override.friction varies. A Coulomb")
	_lines.append("    contact predicts")
	_lines.append("")
	_lines.append("      a = max(0, (F - mu_eff * m * g) / m)")
	_lines.append("")
	_lines.append("    so a case that slides solves for the effective coefficient directly:")
	_lines.append("    mu_eff = (F/m - a) / g. That number, not the property, is what the")
	_lines.append("    M3b solver would be double-counting against its own tire model.")
	_lines.append("")
	_lines.append("    %-30s %10s %10s %10s %10s" % [
		"case", "a frictionless", "a meas", "dz meas", "mu_eff",
	])
	var elapsed := float(TICKS_MEASURE) * _delta
	for spec in _cases:
		var group := String(spec["group"])
		if group != "lateral" and group != "lateral_patch":
			continue
		var body: ProbeBody = spec["body"]
		var ratio: float = float(spec["ratio"])
		var a_free := ratio * g
		var a_measured := (body.linear_velocity.z - Vector3(spec["v0"]).z) / elapsed
		var dz := body.global_position.z - Vector3(spec["p0"]).z
		var mu_effective := (a_free - a_measured) / g
		_lines.append("    %-30s %10.5f %10.5f %10.5f %10.5f" % [
			spec["name"], a_free, a_measured, dz, mu_effective,
		])

	_lines.append("")
	_lines.append("    Tangential contact impulse, i.e. the friction force Jolt applied,")
	_lines.append("    measured directly rather than inferred. Analytic for a sliding")
	_lines.append("    contact is mu_eff * m * g opposing the push.")
	_lines.append("")
	_lines.append("    %-30s %14s %14s" % ["case", "tangential F", "normal F"])
	for spec in _cases:
		var group := String(spec["group"])
		if group != "lateral" and group != "lateral_patch":
			continue
		var samples := maxf(float(spec["push_samples"]), 1.0)
		_lines.append("    %-30s %14.4f %14.4f" % [
			spec["name"],
			float(spec["push_tangent_force"]) / samples,
			float(spec["push_normal_force"]) / samples,
		])

	_lines.append("")
	_lines.append("    The combine rule, pinned on a second slab at friction %.2f." % SLAB_FRICTION)
	_lines.append("    Five candidate rules are printed against the measurement; only one")
	_lines.append("    column can match all three rows.")
	_lines.append("")
	_lines.append("    %-30s %9s %8s %8s %8s %8s %8s" % [
		"case", "mu meas", "min", "product", "sqrt(ab)", "max", "mean",
	])
	var elapsed_combine := float(TICKS_MEASURE) * _delta
	for spec in _cases:
		if String(spec["group"]) != "combine":
			continue
		var body: ProbeBody = spec["body"]
		var a_free := float(spec["ratio"]) * g
		var a_measured := (body.linear_velocity.z - Vector3(spec["v0"]).z) / elapsed_combine
		var mu_measured := (a_free - a_measured) / g
		var a_material := float(spec["friction"])
		_lines.append("    %-30s %9.5f %8.4f %8.4f %8.4f %8.4f %8.4f" % [
			spec["name"], mu_measured,
			minf(a_material, SLAB_FRICTION),
			a_material * SLAB_FRICTION,
			sqrt(a_material * SLAB_FRICTION),
			maxf(a_material, SLAB_FRICTION),
			(a_material + SLAB_FRICTION) * 0.5,
		])


func _report_rays() -> void:
	_lines.append("")
	_lines.append("=== 4. raycast stability and cost =====================================")
	_lines.append("")
	_lines.append("    A %.0f kg box flying level at %.1f m/s and %.2f m above the plane," % [
		KART_MASS, RAY_SPEED, RAY_HEIGHT,
	])
	_lines.append("    %.3f m of travel per tick, rays fired every tick for %d ticks." % [
		RAY_SPEED * _delta, TICKS_MEASURE,
	])
	_lines.append("    'max err' is the worst disagreement with the analytic value over")
	_lines.append("    the whole run, so a distance right on average and wrong while")
	_lines.append("    moving cannot hide in it.")
	_lines.append("")
	_lines.append("    %-24s %8s %8s %11s %11s %10s %10s %8s" % [
		"ray", "hits", "misses", "analytic d", "last d", "max err", "normal err", "self",
	])
	for key: String in _ray_results:
		var entry: Dictionary = _ray_results[key]
		var analytic: float = float(entry["analytic_distance"])
		_lines.append("    %-24s %8d %8d %11s %11s %10.8f %10.8f %8d" % [
			key, entry["hits"], entry["misses"],
			"—" if is_nan(analytic) else "%.6f" % analytic,
			"—" if is_nan(float(entry["last_distance"])) else "%.6f" % float(entry["last_distance"]),
			entry["max_distance_error"], entry["max_normal_error"], entry["self_hits"],
		])

	_lines.append("")
	_lines.append("    last returned normals:")
	for key: String in _ray_results:
		var entry: Dictionary = _ray_results[key]
		_lines.append("      %-24s %s" % [key, entry["last_normal"]])

	_lines.append("")
	_lines.append("    Cost. Wall-clock, machine-specific, gates nothing — the only question")
	_lines.append("    it answers is whether `exclude` is worth avoiding, and a solver firing")
	_lines.append("    4 rays per tick at 240 Hz fires %d per second." % (4 * 240))
	_lines.append("")
	_lines.append("      %-30s %10d us for %d rays  (%.3f us each)" % [
		"with exclude", _ray_cost_us["excluded"], RAY_COST_SAMPLES,
		float(_ray_cost_us["excluded"]) / float(RAY_COST_SAMPLES),
	])
	_lines.append("      %-30s %10d us for %d rays  (%.3f us each)" % [
		"without exclude", _ray_cost_us["included"], RAY_COST_SAMPLES,
		float(_ray_cost_us["included"]) / float(RAY_COST_SAMPLES),
	])


func _report_spin() -> void:
	_lines.append("")
	_lines.append("=== 5. offset force and a custom inertia tensor ========================")
	_lines.append("")
	_lines.append("    In free space, gravity off, inertia set explicitly to %s." % INERTIA)
	_lines.append("    One application of %.0f N at an offset, read one tick later," % SPIN_FORCE)
	_lines.append("    where w = I^-1 (r x F) * dt is exact because w starts at zero and the")
	_lines.append("    gyroscopic term w x Iw is therefore zero too.")
	_lines.append("")
	_lines.append("    Two independent ambiguities are printed as competing predictions:")
	_lines.append("    whether the offset is measured from the body origin or from the center")
	_lines.append("    of mass, and whether it is a world vector or a body-local one.")
	_lines.append("")
	for spec in _cases:
		if String(spec["group"]) != "spin":
			continue
		var body: ProbeBody = spec["body"]
		var basis: Basis = spec["basis0"]
		var force: Vector3 = spec["force"]
		var offset: Vector3 = spec["offset"]
		var com: Vector3 = spec.get("com", Vector3.ZERO)

		# The world-space inverse inertia tensor. `RigidBody3D.inertia` names the
		# principal moments in body space; rotating the body is the only way to
		# make the world tensor non-diagonal, which the second case does.
		var inertia: Vector3 = spec["inertia"]
		var inverse_local := Basis(
			Vector3(1.0 / inertia.x, 0.0, 0.0),
			Vector3(0.0, 1.0 / inertia.y, 0.0),
			Vector3(0.0, 0.0, 1.0 / inertia.z)
		)
		var inverse_world := basis * inverse_local * basis.transposed()

		# Candidate A: offset is a world vector from the center of mass.
		var torque_com_world := offset.cross(force)
		# Candidate B: offset is a world vector from the body origin, so the true
		# lever about the center of mass is longer by (origin - com).
		var torque_origin_world := (offset - basis * com).cross(force)
		# Candidate C: offset is a body-local vector from the center of mass.
		var torque_com_local := (basis * offset).cross(force)

		var alpha_a := inverse_world * torque_com_world
		var alpha_b := inverse_world * torque_origin_world
		var alpha_c := inverse_world * torque_com_local
		var measured: Vector3 = spec["omega_one_tick"]

		_lines.append("    %s" % spec["name"])
		_lines.append("      offset %s   force %s" % [offset, force])
		_lines.append("      %-38s %s" % ["measured dw over one tick", _vector(measured)])
		_lines.append("      %-38s %s   err %.9f" % [
			"A: world offset from center of mass", _vector(alpha_a * _delta),
			(measured - alpha_a * _delta).length(),
		])
		_lines.append("      %-38s %s   err %.9f" % [
			"B: world offset from body origin", _vector(alpha_b * _delta),
			(measured - alpha_b * _delta).length(),
		])
		_lines.append("      %-38s %s   err %.9f" % [
			"C: body-local offset from com", _vector(alpha_c * _delta),
			(measured - alpha_c * _delta).length(),
		])
		var dv_analytic := force / float(spec["mass"]) * _delta
		_lines.append("      %-38s %s" % ["measured dv over one tick", _vector(spec["dv_one_tick"])])
		_lines.append("      %-38s %s   err %.9f" % [
			"analytic dv = F/m * dt", _vector(dv_analytic),
			(Vector3(spec["dv_one_tick"]) - dv_analytic).length(),
		])
		_lines.append("")

	_lines.append("    The same question asked as the vehicle asks it. Two bodies with the")
	_lines.append("    kart's center of mass at %s from the body origin, each given" % KART_COM)
	_lines.append("    %.0f N forward at a contact patch %s from the origin," % [
		KART_DRIVE, Vector3(0.0, 0.0, -1.1),
	])
	_lines.append("    differing only in what offset is handed to apply_force. The physically")
	_lines.append("    intended pitch torque is (contact - com) x F, and it is printed as")
	_lines.append("    'intended': whichever body matches it is applying the drive force at")
	_lines.append("    the contact patch, and the other one is not.")
	_lines.append("")
	for spec in _cases:
		if String(spec["group"]) != "kartlever":
			continue
		var body: ProbeBody = spec["body"]
		var basis: Basis = spec["basis0"]
		var inertia: Vector3 = spec["inertia"]
		var force: Vector3 = spec["force"]
		var com: Vector3 = spec["com"]
		var patch: Vector3 = spec["patch"]
		var inverse_local := Basis(
			Vector3(1.0 / inertia.x, 0.0, 0.0),
			Vector3(0.0, 1.0 / inertia.y, 0.0),
			Vector3(0.0, 0.0, 1.0 / inertia.z)
		)
		var inverse_world := basis * inverse_local * basis.transposed()
		var intended := inverse_world * (patch - basis * com).cross(force) * _delta
		var measured: Vector3 = spec["omega_one_tick"]
		_lines.append("    %s" % spec["name"])
		_lines.append("      %-38s %s" % ["measured dw over one tick", _vector(measured)])
		_lines.append("      %-38s %s   err %.9f" % [
			"intended: (contact - com) x F", _vector(intended), (measured - intended).length(),
		])
		_lines.append("      %-38s %.6f" % [
			"measured / intended pitch (x)", measured.x / intended.x,
		])
		_lines.append("")


func _report_substep() -> void:
	_lines.append("")
	_lines.append("=== 6. ADR-0032 conclusion 3, re-asked with a contact ==================")
	_lines.append("")
	_lines.append("    Four applications of F/4 against one of F, on a body resting on the")
	_lines.append("    plane rather than in free space. The horizontal pair has zero friction")
	_lines.append("    so it slides; the vertical pair straddles m*g so its substeps sum")
	_lines.append("    across the exact transition where the contact stops holding it up.")
	_lines.append("    These must be identical, bit for bit — a difference means a 240 Hz")
	_lines.append("    solver cannot fold its substeps into one application.")
	_lines.append("")
	_lines.append("    %-26s %16s %16s" % ["case", "v (along push)", "displacement"])
	var pairs := [["sub_h1", "sub_h4", "z"], ["sub_v1", "sub_v4", "y"]]
	for pair in pairs:
		var one := _case_by_group(String(pair[0]))
		var four := _case_by_group(String(pair[1]))
		var axis := String(pair[2])
		var body_one: ProbeBody = one["body"]
		var body_four: ProbeBody = four["body"]
		var v_one: float = body_one.linear_velocity.y if axis == "y" else body_one.linear_velocity.z
		var v_four: float = body_four.linear_velocity.y if axis == "y" else body_four.linear_velocity.z
		# Position is compared as a displacement from each body's own baseline,
		# because the two sit in different lanes and their absolute coordinates
		# differ by construction.
		var d_one: float = (body_one.global_position - Vector3(one["p0"]))[
			1 if axis == "y" else 2
		]
		var d_four: float = (body_four.global_position - Vector3(four["p0"]))[
			1 if axis == "y" else 2
		]
		# Printed at nine decimals rather than in scientific notation because
		# GDScript's `%` format silently leaves `%e` in the string as a literal
		# instead of erroring — a trap already recorded in CLAUDE.md.
		_lines.append("    %-26s %16.9f %16.9f" % [one["name"], v_one, d_one])
		_lines.append("    %-26s %16.9f %16.9f" % [four["name"], v_four, d_four])
		_lines.append("    %-26s %16.9f %16.9f" % [
			"  four minus one", v_four - v_one, d_four - d_one,
		])


func _case_by_group(group: String) -> Dictionary:
	for spec in _cases:
		if String(spec["group"]) == group:
			return spec
	return {}


## Vectors printed at a fixed width, because a column that moves is a column two
## runs cannot be diffed by eye.
func _vector(v: Vector3) -> String:
	return "(%12.8f %12.8f %12.8f)" % [v.x, v.y, v.z]
