#ifndef KART_CORE_AUDIO_STATE_H
#define KART_CORE_AUDIO_STATE_H

// The vocabulary the vehicle solver and the audio synthesizer share.
//
// `vehicle_state.h` is the same idea for the physics boundary and says why at
// length: three things are written against these structs by different hands at
// different times, so the vocabulary lives in its own header where none of them
// can quietly widen it. This one has a fourth reason on top — **it crosses a
// thread boundary**, and a struct that crosses a thread boundary has to say so in
// the place a reader will look.
//
// Nothing here may include godot-cpp. ADR-0017.
//
// ## The rate mismatch, which is the whole design problem
//
// The solver produces one `VehicleTelemetry` per 120 Hz tick. The synthesizer
// consumes samples at the device rate, 44.1 or 48 kHz, in blocks whose size the
// audio device chooses. Those are two different clocks and neither drives the
// other: at 48 kHz a 120 Hz tick is 400 samples, and a mix block is whatever Godot
// asks for.
//
// Two consequences, and both are load-bearing:
//
// **1. Every field below is interpolated across the block, not held.** An rpm held
// constant for 400 samples and then stepped is a frequency step of up to a few
// hundred rpm — that is the audible stepping #82's first acceptance criterion
// names, and it comes from the sample-and-hold, not from the synthesis. The synth
// ramps toward the newest state over the block rather than snapping to it.
//
// **2. The producer must never block the consumer.** Whatever transport carries
// this struct, the audio side cannot wait on the physics side and cannot allocate.
// That is why this is a small trivially-copyable POD with no pointers, no
// containers and no virtuals: it is meant to be published by value.
//
// **Which transport is correct on Godot 4.7.1 is a measurement, not an
// assumption**, and it is being measured rather than guessed — issue #81's title
// names `AudioStreamGenerator`, whose `push_buffer` is a ring filled from another
// thread entirely, while its acceptance criterion names work happening *on* the
// audio thread, which only a custom `AudioStreamPlayback::_mix` gives. Those are
// two different architectures. See ADR-0035. This header is deliberately
// indifferent to which one wins: it describes the payload, not the pipe.
//
// ## Why `load` is not `throttle`
//
// A driver's throttle position is an input. What a two-stroke's note responds to is
// how much torque the engine is actually making, which at a given rpm is a
// different number — off the pipe, wide-open throttle makes very little. The
// measured constraint in `kz_audio_reference.h` was extracted by splitting frames
// on the sign of df0/dt, which is an *engine* state and not a pedal, so feeding it
// a pedal would be applying a measurement to something other than what it measured.

namespace kart::core {

// One tick of engine state, as the synthesizer needs it.
//
// Trivially copyable by construction and small on purpose — every field is a
// double or a small integral type, so the whole struct is publishable by value
// through whatever lock-free transport ADR-0035 settles on. Do not add a pointer,
// a container, or anything with a non-trivial copy.
//
// Filled from `VehicleTelemetry` by the boundary, at the 120 Hz tick rate. The
// field-by-field mapping is one-to-one except `load`, which is derived, and that
// derivation lives at the boundary rather than here so that `src/core/` keeps
// having no opinion about who is calling it.
struct EngineAudioInput {
	// Crank speed, rpm. The fundamental is `kz_audio::rpm_to_f0_hz(rpm)`.
	//
	// Comes from `VehicleTelemetry::engine_rpm`, which is end-of-tick state rather
	// than a substep mean — see that struct's comment. That is the right choice
	// here too and for the same reason: an rpm averaged across two substeps
	// describes an engine that is not turning at either of them.
	double rpm = 0.0;

	// How hard the engine is working, 0..1, where 1 is all the torque it has at
	// this rpm.
	//
	// Derived at the boundary as `engine_torque / wide_open_torque(rpm)`, clamped.
	// Negative torque — a trailing throttle, where the engine is being driven by
	// the wheels — clamps to 0 and sets `trailing` below, because the measured
	// on/off-throttle split is a two-state observation and does not extend to
	// signed load.
	double load = 0.0;

	// The driver's actual pedal, 0..1. Carried alongside `load` rather than
	// instead of it, because the two disagree during a shift's torque cut and
	// during clutch slip, and #83 wants both of those audible.
	double throttle = 0.0;

	// True when the engine is being driven by the driveline rather than driving
	// it — `VehicleTelemetry::engine_torque` at or below zero.
	//
	// This is the flag the whole off-throttle ladder tilt keys off. It is a bool
	// and not a fraction because the measurement behind it is a bool: frames were
	// split by the sign of df0/dt into two groups, and nothing in
	// `kz_audio_reference.h` describes an in-between.
	bool trailing = false;

	// Gear, and the states #83 wants to hear. `shifting` is the 50-80 ms torque
	// cut, `over_rev` is past `Engine::hard_cut_rpm`.
	int gear = 0;
	bool shifting = false;
	bool over_rev = false;

	// True while the ignition cut is removing drive — between `soft_cut_rpm` and
	// `hard_cut_rpm`, where `Engine::limiter_scale` is below 1.
	//
	// Separate from `over_rev`, which is past the hard cut and is a mistake rather
	// than a limit. A driver needs to hear the difference: one is the engine doing
	// its job and the other is money leaving.
	bool on_limiter = false;

	// Clutch slip, rad/s, and whether the clutch is transmitting at all. #83's
	// "clutch slip is audible during a launch".
	double clutch_slip = 0.0;

	// Road speed, m/s. For the wind layer, and for distance-independent scaling of
	// anything that should not be audible at a standstill.
	double speed_ms = 0.0;

	// Tire scrub drive, 0..1, aggregated across the four corners at the boundary.
	//
	// §12: "filtered noise modulated by slip magnitude — falls straight out of §6
	// for free". The *modulation* does fall out for free and this is it. The filter
	// shape does not: `kz_audio::SCRUB_SPECTRUM_MEASURED` is false and nothing in
	// the corpus constrains it.
	double scrub = 0.0;

	// Which surface the tires are on, for the scrub layer. `SurfaceType`, carried
	// as an int for the same reason `GroundQuery::surface` is.
	int surface = 0;
};

// The knobs a synthesizer exposes that are not measured quantities.
//
// Split out from `EngineAudioInput` because the two change on completely different
// clocks — the input is per tick, this is per session — and because keeping them
// apart makes it obvious which numbers a driver is changing and which the kart is.
struct EngineAudioConfig {
	// Overall output gain, linear. §15 gives audio 0.5 ms of a 16.6 ms frame; this
	// is not part of that budget, it is just the volume.
	double gain = 0.35;

	// Where the harmonic stack stops filling, Hz. Defaults to
	// `kz_audio::STACK_CEILING_HZ`, which is a tunable and says so.
	double stack_ceiling_hz = 8000.0;

	// Comb delay, seconds. Defaults to `kz_audio::COMB_DELAY_MEASURED_S`, which is
	// a **lower bound measured on a 50 cc pipe** and not a KZ figure. A caller
	// raising it is doing the right thing; a caller treating the default as
	// established is not.
	double comb_delay_s = 0.00142;

	// Comb depth, 0..1. The measured ripple is 1.6-2.6 dB RMS, which is a gentle
	// filter and nothing like the deep metallic comb a first guess reaches for.
	double comb_depth = 0.25;

	// Broadband noise layer, linear, relative to the harmonic stack.
	double noise_gain = 0.12;
};

} // namespace kart::core

#endif // KART_CORE_AUDIO_STATE_H
