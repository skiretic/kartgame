#ifndef KARTGAME_TRACK_KART_TRACK_H
#define KARTGAME_TRACK_KART_TRACK_H

#include "core/track.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/vector3.hpp>

namespace kartgame {

// `track.json`'s runtime consumer: collision, checkpoints, grid and projection.
//
// `ARCHITECTURE.md` §11's "one definition, three consumers", and ADR-0046 owns
// the schema. `docs/TRACK_SCHEMA.md` is the format; `src/core/track.h` is the
// geometry and the validation pass with no engine in it; **this class is the
// seam** - it parses JSON with Godot's parser, converts the strings the file uses
// into the integers `src/core/surface.h` calls a wire format, and builds the
// triangles.
//
// The split is not decoration. Everything that can be arithmetic is arithmetic in
// `src/core/`, where `tests/run.sh` reaches it in five seconds with no engine at
// all; everything that needs a `String`, a `FileAccess` or a `PackedVector3Array`
// is here. That is the same boundary ADR-0017 drew for the vehicle and the same
// reason.
//
// ## What a caller has to do, in order
//
//     var track := KartTrack.new()
//     if track.load("res://data/tracks/valdirone_nuova.track.json") != OK:
//         push_error("\n".join(track.problems()))
//     track.select_layout("reverse")
//
// `load` **refuses** a track that fails validation rather than warning about it.
// A track that loads and cannot be raced is worse than one that will not load:
// the failure surfaces three hundred meters into a session, at a corner nobody
// can take, as a physics bug. `problems()` is populated either way, so a caller
// that wants to inspect a broken file can.
//
// ## Collision, and why the surfaces come back separated
//
// `surface_meshes()` returns one entry per surface *type*, not one mesh. A
// `StaticBody3D` carries `surface_type` as node metadata - that is how
// `KartBody::query_ground` learns what a wheel is standing on - and metadata is
// per body, so asphalt, kerb and gravel have to be three bodies. Handing back one
// merged soup would make the whole circuit read as asphalt, which is the failure
// that looks like a tire model bug.
//
// The visual mesh comes from `tools/blender/gentrack.py` reading the same file
// through the same rules. They are two implementations of one specification and
// `tools/verify/circuit.sh` measures them against each other rather than trusting
// them, because "what you see and what you collide with cannot drift apart" is
// only true if something checks.
class KartTrack : public godot::RefCounted {
	GDCLASS(KartTrack, godot::RefCounted)

public:
	// The schema version this build speaks. A file that says anything else is
	// refused rather than migrated - ADR-0046, and the opposite of ADR-0042's rule
	// for saves, because a track is authored project data under version control.
	static const int SCHEMA_VERSION = 1;

	KartTrack() = default;
	~KartTrack() override = default;

	godot::Error load(const godot::String &p_path);
	bool is_loaded() const { return _loaded; }
	godot::PackedStringArray problems() const;

	godot::String track_name() const;
	godot::String source_path() const { return _path; }
	double length() const { return _track.length_m; }
	int grade() const { return _track.grade; }
	// FNV-1a over the file's bytes. ADR-0041 puts the track's content hash into
	// `SessionConfig`, so a replay recorded before a corner was smoothed refuses to
	// play back and says which file moved - rather than re-simulating against
	// different collision geometry and reporting a determinism failure that is
	// really a content change.
	godot::String content_hash() const { return _content_hash; }

	godot::PackedStringArray layout_names() const;
	bool select_layout(const godot::String &p_name);
	godot::String layout() const { return _layout_name; }
	bool is_reversed() const;

	// --- geometry, in the selected layout's stations ----------------------

	godot::Dictionary sample(double p_station) const;
	godot::Dictionary project(const godot::Vector3 &p_position, double p_hint = -1.0) const;
	godot::PackedVector3Array centerline(double p_sagitta = 0.02, double p_max_spacing = 2.0) const;
	// A layout station from a forward centerline distance and back. Its own
	// inverse, which is what makes a reversal an involution rather than a pair of
	// functions that can disagree.
	double to_station(double p_forward_distance) const;
	double to_forward(double p_station) const;

	// --- furniture --------------------------------------------------------

	godot::PackedFloat64Array checkpoints() const;
	godot::PackedFloat64Array sector_marks() const;
	int grid_count() const;
	// Placed on the road surface at the slot's own cross-section, facing the
	// layout's direction of travel. `p_lift` is added along the surface normal, for
	// the reason `proving_ground.gd` gives: spawning at exactly wheel height starts
	// the first tick with the tires interpenetrating, and Jolt resolves that by
	// pushing them apart. A kart that hops on spawn is this, not a suspension bug.
	godot::Transform3D grid_transform(int p_index, double p_lift = 0.12) const;

	int corner_count() const;
	godot::Dictionary corner(int p_index) const;
	godot::Dictionary measurements() const;

	// --- the pit lane -----------------------------------------------------
	//
	// Both in **forward** stations, not in the selected layout's, and deliberately:
	// there is one pit lane and there are four junctions, and all five exist on the
	// ground whichever way the circuit is being driven. A caller that wants the
	// selected layout's own stations has `to_station` for that; a caller that wants
	// to know whether a kart is in the pits asks the station and not the surface,
	// because pit asphalt is asphalt.
	godot::Dictionary pit_lane() const;
	godot::TypedArray<godot::Dictionary> pit_stubs() const;

	// --- collision --------------------------------------------------------

	// One entry per surface type: `{name, surface_type, faces}`. See the class
	// comment for why they are not merged.
	godot::TypedArray<godot::Dictionary> surface_meshes(double p_sagitta = 0.02,
			double p_max_spacing = 2.0) const;

protected:
	static void _bind_methods();

private:
	kart::core::track::Track _track;
	godot::PackedStringArray _problems;
	godot::String _path;
	godot::String _content_hash;
	godot::String _layout_name;
	int _layout_index = -1;
	bool _loaded = false;

	const kart::core::track::Layout *_layout() const;
	godot::Dictionary _frame_to_dictionary(const kart::core::track::Frame &p_frame) const;
	godot::Vector3 _surface_point(const kart::core::track::Frame &p_frame, double p_lateral,
			double p_lift) const;
	// Where a barrier stands: the cross-section's x and z, but the road's own
	// centerline elevation minus the terrain's shoulder drop for y. Issue #244 -
	// see the definition for why this is not `_surface_point`.
	godot::Vector3 _barrier_base(const kart::core::track::Frame &p_frame,
			double p_lateral) const;
	double _ramp(const kart::core::track::SurfaceSpan &p_span, double p_distance) const;
};

}  // namespace kartgame

#endif  // KARTGAME_TRACK_KART_TRACK_H
