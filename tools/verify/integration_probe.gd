extends SceneTree

## Issue #28 — measure exactly how a force applied from GDScript reaches a Jolt
## body, before M3b's solver is built on an assumption about it.
##
##     godot --headless --path . --script tools/verify/integration_probe.gd
##
## `ARCHITECTURE.md` §19 names this as the first risk in the plan: "applying
## per-wheel forces to a `RigidBody3D` from `_physics_process`, while substepping
## internally at 240 Hz, is the least-charted part". Four questions, each with an
## analytic answer to check against, all in free space with no contacts and no
## gravity so that nothing but the integrator is under test:
##
##   1. What integration scheme does the body actually use? Symplectic Euler and
##      explicit Euler differ by exactly one step of `a*dt²` after N steps, which
##      is measurable to eleven digits.
##   2. Does it matter whether the force is applied from `_physics_process` or
##      from `_integrate_forces`?
##   3. Do repeated applications inside one tick accumulate, or does the last one
##      win? This is the question a 240 Hz substepped solver inside a 120 Hz tick
##      turns on.
##   4. Is a force sticky — does one call keep pushing on later ticks?
##
## The answers are in ADR-0032. This file is what produced them, and it is kept
## so they can be re-measured against a future engine rather than trusted.

## Test mass, kg. Arbitrary and not the kart's: this measures the integrator, and
## a round number makes a wrong answer legible.
const MASS := 10.0

## Applied force, newtons, along +X. 100 N on 10 kg is 10 m/s² — one gravity,
## which makes the expected numbers easy to check by eye.
const FORCE := 100.0

const TICKS := 120

var _cases: Array[Dictionary] = []
var _bodies: Array[RigidBody3D] = []
var _tick := 0
var _lines: Array[String] = []


func _initialize() -> void:
	# Each case is one body and one way of pushing it. They run in the same world
	# at the same time, so a difference between them cannot be a difference in
	# anything else.
	_cases = [
		{"name": "physics_process, force", "mode": "process"},
		{"name": "integrate_forces, force", "mode": "integrate"},
		{"name": "4 sub-applications of F/4", "mode": "substep"},
		{"name": "impulse F*dt each tick", "mode": "impulse"},
		{"name": "one force, tick 0 only", "mode": "once"},
	]

	for case_index in _cases.size():
		var body := RigidBody3D.new()
		body.mass = MASS
		# Everything that could add a force other than the one under test, off.
		# `linear_damp` in particular defaults to 0.1 project-wide and is worth
		# 1 m/s² at 10 m/s — ADR-0031 found it eating a quarter of the kart's
		# acceleration.
		body.gravity_scale = 0.0
		body.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
		body.linear_damp = 0.0
		body.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
		body.angular_damp = 0.0
		body.can_sleep = false
		# A body with no collision shape still integrates, and having none means
		# no contact can enter the measurement. Bodies are spaced apart anyway so
		# that a future reader adding a shape does not silently start measuring
		# collisions.
		body.position = Vector3(0.0, float(case_index) * 10.0, 0.0)
		body.name = "Case%d" % case_index
		get_root().add_child(body)
		_bodies.append(body)


func _physics_process(delta: float) -> bool:
	if _tick >= TICKS:
		_report(delta)
		return true

	for case_index in _cases.size():
		var body := _bodies[case_index]
		match String(_cases[case_index]["mode"]):
			"process":
				body.apply_central_force(Vector3(FORCE, 0.0, 0.0))
			"integrate":
				# Applied through the direct state instead, which is the hook
				# Godot's own docs point at for "custom physics". Whether that is
				# a different thing from the line above is question 2.
				var state := PhysicsServer3D.body_get_direct_state(body.get_rid())
				state.apply_central_force(Vector3(FORCE, 0.0, 0.0))
			"substep":
				# What a 240 Hz solver inside a 120 Hz tick has to do: it cannot
				# ask the engine to step twice, so its substeps have to combine
				# into what it applies once.
				for substep in 4:
					body.apply_central_force(Vector3(FORCE / 4.0, 0.0, 0.0))
			"impulse":
				body.apply_central_impulse(Vector3(FORCE * delta, 0.0, 0.0))
			"once":
				if _tick == 0:
					body.apply_central_force(Vector3(FORCE, 0.0, 0.0))

	_tick += 1
	return false


func _report(delta: float) -> void:
	var acceleration := FORCE / MASS
	var elapsed := float(TICKS) * delta

	# Continuous solution, for scale. Nothing should match this exactly — a
	# fixed-step integrator cannot.
	var analytic_v := acceleration * elapsed
	var analytic_x := 0.5 * acceleration * elapsed * elapsed

	# The two candidate discrete answers. They differ by exactly one step of
	# a*dt^2 over the whole run, which at these numbers is 6.9 mm against 7.0 m —
	# small, unambiguous, and the entire point of measuring rather than assuming.
	var symplectic_x := acceleration * delta * delta * float(TICKS * (TICKS + 1)) / 2.0
	var explicit_x := acceleration * delta * delta * float(TICKS * (TICKS - 1)) / 2.0

	_lines.append("--- %d ticks at %.6f s, %.1f N on %.1f kg (a = %.3f m/s^2)" % [
		TICKS, delta, FORCE, MASS, acceleration,
	])
	_lines.append("    analytic continuous     v %10.6f   x %10.6f" % [analytic_v, analytic_x])
	_lines.append("    discrete symplectic     v %10.6f   x %10.6f" % [analytic_v, symplectic_x])
	_lines.append("    discrete explicit       v %10.6f   x %10.6f" % [analytic_v, explicit_x])
	_lines.append("")

	for case_index in _cases.size():
		var body := _bodies[case_index]
		var v: float = body.linear_velocity.x
		var x: float = body.position.x
		# The tolerance is 1e-4, not 1e-6. The two candidate schemes are 83 mm
		# apart here, and the measurement sits about 1.4e-6 off its match —
		# single-precision drift accumulated over 120 additions, which is the
		# engine's float width (`KartCore.build_info()` reports it) and not an
		# error in either model. A tolerance tighter than the arithmetic would
		# label the right answer "unknown".
		var scheme := "no match"
		if absf(x - symplectic_x) < 1e-4:
			scheme = "symplectic, residual %+.7f m" % (x - symplectic_x)
		elif absf(x - explicit_x) < 1e-4:
			scheme = "explicit, residual %+.7f m" % (x - explicit_x)
		_lines.append("    %-26s v %10.6f   x %10.6f   %s" % [
			_cases[case_index]["name"], v, x, scheme,
		])

	print("\n".join(_lines))
	quit(0)
