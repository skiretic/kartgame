#include "kart_replay.h"

#include "session/fsync_shim.h"
#include "version.h"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
using namespace kart::core;

namespace kartgame {

namespace {

// The line that separates the text header from the binary body.
//
// It is a **comment** in the header format on purpose, so that a reader which
// hands the whole text region to `replay_parse_header` skips it rather than
// failing on an unknown key. It carries both lengths, which is what makes a
// truncated file a length mismatch rather than a body read at the wrong stride —
// the failure `replay.h` warns about, where a wrong stride "decodes into
// plausible garbage rather than into an error".
//
// ASCII only, like everything else in a literal here: `godot::String` decodes a
// bare `const char *` as Latin-1, so a non-ASCII byte in this line would reach
// Godot as mojibake and, worse, would change the marker's length.
const char *BODY_MARKER = "# body ";
const char *FOOTER_WORD = " footer ";

String from_ascii(const char *text) {
	return String::utf8(text);
}

// A Dictionary in `input_driver`'s shape to the struct. Reads with defaults, the
// same six keys `KartBody::gather_input` reads, so a producer hands one object to
// both and cannot have them disagree.
DriverInput input_from_dictionary(const Dictionary &values) {
	// Function-local statics. A namespace-scope `StringName` in a GDExtension runs
	// its constructor inside `dlopen`, before godot-cpp has bound its interface,
	// and crashes in `dyld4::callInitializer` before any Godot frame exists.
	static const StringName KEY_THROTTLE("throttle");
	static const StringName KEY_BRAKE("brake");
	static const StringName KEY_STEER("steer");
	static const StringName KEY_CLUTCH("clutch");
	static const StringName KEY_SHIFT_UP("shift_up");
	static const StringName KEY_SHIFT_DOWN("shift_down");

	DriverInput input;
	input.throttle = static_cast<double>(values.get(KEY_THROTTLE, 0.0));
	input.brake = static_cast<double>(values.get(KEY_BRAKE, 0.0));
	input.steer = static_cast<double>(values.get(KEY_STEER, 0.0));
	input.clutch = static_cast<double>(values.get(KEY_CLUTCH, 0.0));
	input.shift_up = static_cast<bool>(values.get(KEY_SHIFT_UP, false));
	input.shift_down = static_cast<bool>(values.get(KEY_SHIFT_DOWN, false));
	return input;
}

Dictionary dictionary_from_input(const DriverInput &input) {
	static const StringName KEY_THROTTLE("throttle");
	static const StringName KEY_BRAKE("brake");
	static const StringName KEY_STEER("steer");
	static const StringName KEY_CLUTCH("clutch");
	static const StringName KEY_SHIFT_UP("shift_up");
	static const StringName KEY_SHIFT_DOWN("shift_down");

	Dictionary values;
	values[KEY_THROTTLE] = input.throttle;
	values[KEY_BRAKE] = input.brake;
	values[KEY_STEER] = input.steer;
	values[KEY_CLUTCH] = input.clutch;
	values[KEY_SHIFT_UP] = input.shift_up;
	values[KEY_SHIFT_DOWN] = input.shift_down;
	return values;
}

// Strip a trailing slash and make sure the directory exists, so a save that would
// have failed only because `user://replays/` did not exist does not fail. Same
// shape and same reason as `kart_profile.cpp`'s.
String normalize_dir(const String &raw) {
	String dir = raw.strip_edges();
	if (dir.is_empty()) {
		return String();
	}
	while (dir.length() > 1 && dir.ends_with("/")) {
		dir = dir.substr(0, dir.length() - 1);
	}
	if (dir == "user:/" || dir == "user:") {
		dir = String("user://");
	}
	if (dir == "res:/" || dir == "res:") {
		return String();
	}
	if (dir != "user://" && !DirAccess::dir_exists_absolute(dir)) {
		if (DirAccess::make_dir_recursive_absolute(dir) != OK) {
			return String();
		}
	}
	return dir;
}

// Write `bytes` to `target` through `temp`, then rename over it.
//
// A copy of `kart_profile.cpp`'s `atomic_store`, and copied rather than shared
// because that one is file-local to a translation unit this class must not depend
// on. The three things that make it work are unchanged and each of them is a bug
// this project has already paid for:
//
//   * **The target is never opened.** `FileAccess.open(path, WRITE)` truncates on
//     open — measured 24 bytes before the call and 0 immediately after — so a
//     direct write leaves an empty file rather than a half-written one for the
//     whole window.
//   * **The bytes are synced before any name points at them.** Godot's
//     `FileAccess` has 68 methods and none of them sync, which is why
//     `fsync_shim.h` exists at all (issue #173). On Darwin a plain `fsync` does
//     not flush the drive cache, so the shim names `F_FULLFSYNC`.
//   * **The temporary is in the target's own directory.**
//     `FileAccess::create_temp` lands in the OS temp dir, often another
//     filesystem, where `rename` fails `EXDEV`.
Error atomic_store(const String &target, const String &temp, const PackedByteArray &bytes,
		PackedStringArray &warnings) {
	Ref<FileAccess> file = FileAccess::open(temp, FileAccess::WRITE);
	if (file.is_null()) {
		const Error why = FileAccess::get_open_error();
		warnings.push_back(vformat("cannot write %s (error %d)", temp, why));
		return why == OK ? ERR_FILE_CANT_WRITE : why;
	}
	const bool stored = file->store_buffer(bytes);
	// Flush before close rather than trusting close to do it. `close()` does flush;
	// what matters is that "the bytes are with the kernel before the rename is
	// issued" is stated here rather than inherited.
	file->flush();
	file->close();
	if (!stored) {
		warnings.push_back(vformat("the write to %s did not complete", temp));
		DirAccess::remove_absolute(temp);
		return ERR_FILE_CANT_WRITE;
	}

	// Read the length back off disk. `store_buffer` returning true says the calls
	// were accepted, not that the bytes landed, and a full disk is where those
	// differ. A replay body is megabytes, so this is the realistic case.
	{
		Ref<FileAccess> check = FileAccess::open(temp, FileAccess::READ);
		if (check.is_null()) {
			warnings.push_back(vformat("wrote %s and could not reopen it", temp));
			return ERR_FILE_CANT_READ;
		}
		const int64_t on_disk = static_cast<int64_t>(check->get_length());
		check->close();
		if (on_disk != bytes.size()) {
			warnings.push_back(vformat("%s holds %d bytes of %d -- out of space?", temp, on_disk,
					bytes.size()));
			DirAccess::remove_absolute(temp);
			return ERR_FILE_CANT_WRITE;
		}
	}

	const Error synced = fsync_file(temp);
	if (synced == ERR_UNAVAILABLE) {
		warnings.push_back(vformat(
				"%s cannot sync; this replay is safe against a crash, not a power cut", temp));
	} else if (synced != OK) {
		warnings.push_back(vformat("the sync of %s failed (error %d)", temp, synced));
		DirAccess::remove_absolute(temp);
		return ERR_FILE_CANT_WRITE;
	}

	const Error renamed = DirAccess::rename_absolute(temp, target);
	if (renamed != OK) {
		warnings.push_back(vformat("cannot rename %s over %s (error %d)", temp, target, renamed));
		DirAccess::remove_absolute(temp);
		return renamed;
	}

	if (fsync_dir(target.get_base_dir()) != OK) {
		warnings.push_back(vformat(
				"the directory of %s did not sync; a power cut may revert to the previous file",
				target));
	}
	return OK;
}

} // namespace

// --- binding -----------------------------------------------------------------

void KartReplay::_bind_methods() {
	ClassDB::bind_static_method("KartReplay", D_METHOD("snap_input", "input"),
			&KartReplay::snap_input);
	ClassDB::bind_static_method("KartReplay", D_METHOD("is_on_grid", "input"),
			&KartReplay::is_on_grid);

	ClassDB::bind_method(D_METHOD("begin_record", "session"), &KartReplay::begin_record);
	ClassDB::bind_method(D_METHOD("set_hash_interval", "interval"), &KartReplay::set_hash_interval);
	ClassDB::bind_method(D_METHOD("get_hash_interval"), &KartReplay::get_hash_interval);
	ClassDB::bind_method(D_METHOD("record_input", "input"), &KartReplay::record_input);
	ClassDB::bind_method(D_METHOD("record_tick", "inputs"), &KartReplay::record_tick);
	ClassDB::bind_method(D_METHOD("record_checkpoint", "tick", "hash_hex"),
			&KartReplay::record_checkpoint);
	ClassDB::bind_method(D_METHOD("is_checkpoint_tick", "tick"), &KartReplay::is_checkpoint_tick);
	ClassDB::bind_method(D_METHOD("finalize"), &KartReplay::finalize);
	ClassDB::bind_method(D_METHOD("is_finalized"), &KartReplay::is_finalized);

	ClassDB::bind_method(D_METHOD("save", "path"), &KartReplay::save);
	ClassDB::bind_method(D_METHOD("load", "path"), &KartReplay::load);
	ClassDB::bind_method(D_METHOD("warnings"), &KartReplay::warnings);
	ClassDB::bind_method(D_METHOD("parse_problem"), &KartReplay::parse_problem);
	ClassDB::bind_method(D_METHOD("parse_line"), &KartReplay::parse_line);
	ClassDB::bind_method(D_METHOD("parse_key"), &KartReplay::parse_key);

	ClassDB::bind_method(D_METHOD("get_format_version"), &KartReplay::get_format_version);
	ClassDB::bind_method(D_METHOD("get_build"), &KartReplay::get_build);
	ClassDB::bind_method(D_METHOD("get_api_version"), &KartReplay::get_api_version);
	ClassDB::bind_method(D_METHOD("get_tick_count"), &KartReplay::get_tick_count);
	ClassDB::bind_method(D_METHOD("get_kart_count"), &KartReplay::get_kart_count);
	ClassDB::bind_method(D_METHOD("config_hash_hex"), &KartReplay::config_hash_hex);
	ClassDB::bind_method(D_METHOD("to_session"), &KartReplay::to_session);
	ClassDB::bind_method(D_METHOD("header_text"), &KartReplay::header_text);

	ClassDB::bind_method(D_METHOD("input_at", "tick", "kart"), &KartReplay::input_at);
	ClassDB::bind_method(D_METHOD("checkpoint_count"), &KartReplay::checkpoint_count);
	ClassDB::bind_method(D_METHOD("checkpoint_tick", "index"), &KartReplay::checkpoint_tick);
	ClassDB::bind_method(D_METHOD("checkpoint_hash_hex", "index"),
			&KartReplay::checkpoint_hash_hex);
	ClassDB::bind_method(D_METHOD("checkpoint_hash_at", "tick"), &KartReplay::checkpoint_hash_at);

	ClassDB::bind_method(D_METHOD("begin_playback", "live"), &KartReplay::begin_playback);
	ClassDB::bind_method(D_METHOD("compare_checkpoint", "tick", "live_hash_hex"),
			&KartReplay::compare_checkpoint);
	ClassDB::bind_method(D_METHOD("compare_footer", "live_hashes"), &KartReplay::compare_footer);
	ClassDB::bind_method(D_METHOD("verdict"), &KartReplay::verdict);
	ClassDB::bind_method(D_METHOD("verdict_name"), &KartReplay::verdict_name);
	ClassDB::bind_method(D_METHOD("refusal_reason"), &KartReplay::refusal_reason);
	ClassDB::bind_method(D_METHOD("refusal_name"), &KartReplay::refusal_name);
	ClassDB::bind_method(D_METHOD("describe"), &KartReplay::describe);
	ClassDB::bind_method(D_METHOD("diverged_tick"), &KartReplay::diverged_tick);
	ClassDB::bind_method(D_METHOD("recorded_hash_hex"), &KartReplay::recorded_hash_hex);
	ClassDB::bind_method(D_METHOD("live_hash_hex"), &KartReplay::live_hash_hex);
	ClassDB::bind_method(D_METHOD("checkpoints_compared"), &KartReplay::checkpoints_compared);
	ClassDB::bind_method(D_METHOD("build_warning"), &KartReplay::build_warning);
	ClassDB::bind_method(D_METHOD("api_warning"), &KartReplay::api_warning);

	ClassDB::bind_method(D_METHOD("perturb_input", "tick", "kart", "axis"),
			&KartReplay::perturb_input);
	ClassDB::bind_method(D_METHOD("truncate_body", "bytes"), &KartReplay::truncate_body);

	BIND_CONSTANT(VERDICT_PASSED);
	BIND_CONSTANT(VERDICT_DIVERGED);
	BIND_CONSTANT(VERDICT_REFUSED);
	BIND_CONSTANT(REFUSAL_NONE);
	BIND_CONSTANT(REFUSAL_FORMAT_VERSION);
	BIND_CONSTANT(REFUSAL_HEADER_CORRUPT);
	BIND_CONSTANT(REFUSAL_CONFIG_MISMATCH);
	BIND_CONSTANT(REFUSAL_TRUNCATED_BODY);
	BIND_CONSTANT(FORMAT_VERSION);
	BIND_CONSTANT(HASH_INTERVAL);
	BIND_CONSTANT(INPUT_BYTES);
	BIND_CONSTANT(CHECKPOINT_BYTES);
}

// --- identity -----------------------------------------------------------------

String KartReplay::this_build() {
	// Everything that distinguishes two builds of this project, in the 48
	// characters `REPLAY_BUILD_CHARS` allows. "0.1.0-m0 editor macos arm64" is 27.
	return vformat("%s %s %s %s", from_ascii(KARTGAME_VERSION),
			from_ascii(KARTGAME_STRINGIFY(KARTGAME_BUILD_TARGET)),
			from_ascii(KARTGAME_STRINGIFY(KARTGAME_BUILD_PLATFORM)),
			from_ascii(KARTGAME_STRINGIFY(KARTGAME_BUILD_ARCH)));
}

String KartReplay::this_api_version() {
	const Dictionary version = Engine::get_singleton()->get_version_info();
	static const StringName KEY_MAJOR("major");
	static const StringName KEY_MINOR("minor");
	static const StringName KEY_PATCH("patch");
	return vformat("%d.%d.%d", static_cast<int>(version.get(KEY_MAJOR, 0)),
			static_cast<int>(version.get(KEY_MINOR, 0)),
			static_cast<int>(version.get(KEY_PATCH, 0)));
}

String KartReplay::hex64(uint64_t value) {
	// **Not `pad_zeros`.** It counts only *digit* characters before deciding how
	// many zeros a string is short, so a hex digest beginning with a letter has its
	// padding inserted after the letters and one made entirely of letters is padded
	// as if it were empty: `"ffffffffffffffff".pad_zeros(16)` returns 32 characters.
	// `KartStateHash::hex` pads by hand and so does this, so the two produce strings
	// that can be compared.
	String digits = String::num_uint64(value, 16);
	while (digits.length() < 16) {
		digits = "0" + digits;
	}
	return digits;
}

bool KartReplay::parse_hex64(const String &text, uint64_t &out) {
	String trimmed = text.strip_edges();
	if (trimmed.begins_with("0x") || trimmed.begins_with("0X")) {
		trimmed = trimmed.substr(2);
	}
	if (trimmed.is_empty() || trimmed.length() > 16) {
		return false;
	}
	uint64_t value = 0;
	for (int i = 0; i < trimmed.length(); ++i) {
		const char32_t c = trimmed[i];
		int nibble = -1;
		if (c >= '0' && c <= '9') {
			nibble = static_cast<int>(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			nibble = static_cast<int>(c - 'a') + 10;
		} else if (c >= 'A' && c <= 'F') {
			nibble = static_cast<int>(c - 'A') + 10;
		} else {
			return false;
		}
		value = (value << 4) | static_cast<uint64_t>(nibble);
	}
	out = value;
	return true;
}

// --- the producer's half --------------------------------------------------------

Dictionary KartReplay::snap_input(const Dictionary &p_input) {
	return dictionary_from_input(replay_snap(input_from_dictionary(p_input)));
}

bool KartReplay::is_on_grid(const Dictionary &p_input) {
	return replay_is_on_grid(input_from_dictionary(p_input));
}

// --- recording --------------------------------------------------------------------

bool KartReplay::begin_record(const Ref<KartSession> &p_session) {
	if (p_session.is_null()) {
		ERR_PRINT("KartReplay.begin_record: no session");
		return false;
	}
	if (!p_session->is_valid()) {
		// Refused here rather than at `replay_admit`, which is a full
		// re-simulation later and a long way from the cause.
		ERR_PRINT("KartReplay.begin_record: the session is not valid, so nothing "
				  "recorded from it could ever be admitted");
		return false;
	}
	const int interval = header_.hash_interval;
	header_ = ReplayHeader();
	header_.hash_interval = interval;
	header_.config = p_session->config();
	header_.set_build(this_build().utf8().get_data());
	header_.set_api_version(this_api_version().utf8().get_data());
	header_.tick_count = 0;
	header_.stamp();

	body_.clear();
	footer_.clear();
	recorded_ticks_ = 0;
	recording_ = true;
	finalized_ = false;
	loaded_ = false;
	playing_ = false;
	report_ = PlaybackReport();
	warnings_.clear();
	return true;
}

bool KartReplay::set_hash_interval(int p_interval) {
	if (p_interval < 1) {
		return false;
	}
	if (recorded_ticks_ > 0 || footer_.size() > 0) {
		// Moving the interval mid-recording would leave a footer whose entries are
		// at ticks the header's arithmetic does not predict, which is the one thing
		// `checkpoint_count()` may not be wrong about.
		ERR_PRINT("KartReplay.set_hash_interval: too late, this recording has started");
		return false;
	}
	header_.hash_interval = p_interval;
	if (recording_) {
		header_.stamp();
	}
	return true;
}

int KartReplay::get_hash_interval() const {
	return header_.hash_interval;
}

bool KartReplay::record_input(const Dictionary &p_input) {
	Array one;
	one.push_back(p_input);
	return record_tick(one);
}

bool KartReplay::record_tick(const Array &p_inputs) {
	if (!recording_ || finalized_) {
		ERR_PRINT("KartReplay.record_tick: not recording");
		return false;
	}
	const int karts = header_.kart_count();
	if (p_inputs.size() != karts) {
		// `replay.h` has no `kart_count` field so that a second copy cannot
		// disagree with the first. This is that disagreement arriving one layer up,
		// and it is refused for the same reason: a body read at the wrong stride
		// decodes into plausible garbage rather than into an error.
		ERR_PRINT(vformat("KartReplay.record_tick: %d inputs for a %d-kart session",
				p_inputs.size(), karts));
		return false;
	}
	unsigned char scratch[REPLAY_INPUT_BYTES];
	// Encoded into a scratch buffer first, so that a refusal on kart 3 of 4 leaves
	// the body exactly as it was rather than half a tick longer.
	PackedByteArray tick;
	tick.resize(karts * REPLAY_INPUT_BYTES);
	uint8_t *out = tick.ptrw();
	for (int kart = 0; kart < karts; ++kart) {
		if (p_inputs[kart].get_type() != Variant::DICTIONARY) {
			ERR_PRINT(vformat("KartReplay.record_tick: input %d is not a Dictionary", kart));
			return false;
		}
		const DriverInput input = input_from_dictionary(p_inputs[kart]);
		if (replay_encode_input(input, scratch, REPLAY_INPUT_BYTES) < 0) {
			// **The refusal ADR-0041 exists for.** This does not round. If input were
			// quantized on write the live run would have consumed full-precision
			// values and the replay rounded ones, and the divergence that follows
			// looks exactly like a solver bug. Call `snap_input` upstream of the
			// vehicle instead.
			ERR_PRINT(vformat("KartReplay.record_tick: kart %d's input is not on the storage "
							  "grid. Call KartReplay.snap_input() upstream of the solver -- "
							  "throttle %f brake %f steer %f clutch %f",
					kart, input.throttle, input.brake, input.steer, input.clutch));
			return false;
		}
		for (int byte = 0; byte < REPLAY_INPUT_BYTES; ++byte) {
			out[kart * REPLAY_INPUT_BYTES + byte] = scratch[byte];
		}
	}
	body_.append_array(tick);
	++recorded_ticks_;
	return true;
}

bool KartReplay::record_checkpoint(int64_t p_tick, const String &p_hash_hex) {
	if (!recording_ || finalized_) {
		ERR_PRINT("KartReplay.record_checkpoint: not recording");
		return false;
	}
	if (p_tick < 0 || !header_.is_checkpoint_tick(static_cast<uint64_t>(p_tick))) {
		// A footer entry at a tick the interval does not call for is an entry the
		// re-simulation will never produce, and `replay_compare_footer` would report
		// the misalignment as a divergence on a tick nothing went wrong at.
		ERR_PRINT(vformat("KartReplay.record_checkpoint: tick %d is not a multiple of the "
						  "hash interval %d",
				p_tick, header_.hash_interval));
		return false;
	}
	uint64_t digest = 0;
	if (!parse_hex64(p_hash_hex, digest)) {
		ERR_PRINT(vformat("KartReplay.record_checkpoint: '%s' is not a 64-bit hex hash",
				p_hash_hex));
		return false;
	}
	ReplayCheckpoint point;
	point.tick = static_cast<uint64_t>(p_tick);
	point.hash = digest;
	unsigned char scratch[REPLAY_CHECKPOINT_BYTES];
	if (replay_encode_checkpoint(point, scratch, REPLAY_CHECKPOINT_BYTES) < 0) {
		return false;
	}
	for (int byte = 0; byte < REPLAY_CHECKPOINT_BYTES; ++byte) {
		footer_.push_back(scratch[byte]);
	}
	return true;
}

bool KartReplay::is_checkpoint_tick(int64_t p_tick) const {
	return p_tick >= 0 && header_.is_checkpoint_tick(static_cast<uint64_t>(p_tick));
}

bool KartReplay::finalize() {
	if (!recording_) {
		ERR_PRINT("KartReplay.finalize: nothing was recorded");
		return false;
	}
	header_.tick_count = static_cast<uint64_t>(recorded_ticks_);
	header_.stamp();
	if (!header_.is_valid()) {
		ERR_PRINT("KartReplay.finalize: the header is not valid");
		return false;
	}
	// The footer has to hold exactly the entries the header's own arithmetic
	// predicts. A recorder that skipped one leaves `replay_compare_footer`
	// comparing a recorded tick against a live tick one entry further on, and the
	// misalignment is reported as a divergence at a tick nothing went wrong at --
	// a determinism alarm caused entirely by the harness.
	const int64_t expected = static_cast<int64_t>(header_.checkpoint_count());
	const int64_t have = footer_.size() / REPLAY_CHECKPOINT_BYTES;
	if (have != expected) {
		ERR_PRINT(vformat("KartReplay.finalize: %d checkpoints for %d ticks at an interval of "
						  "%d, which wants %d. A skipped checkpoint is reported later as a "
						  "divergence at a tick nothing went wrong at.",
				have, static_cast<int64_t>(header_.tick_count), header_.hash_interval, expected));
		return false;
	}
	finalized_ = true;
	return true;
}

bool KartReplay::is_finalized() const {
	return finalized_;
}

// --- the file ---------------------------------------------------------------------

Error KartReplay::save(const String &p_path) {
	warnings_.clear();
	if (!finalized_ && !loaded_) {
		ERR_PRINT("KartReplay.save: finalize() first -- a header declaring zero ticks over a "
				  "full body is a file that reads as empty");
		return ERR_UNCONFIGURED;
	}
	if (p_path.strip_edges().is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	const String dir = normalize_dir(p_path.get_base_dir());
	if (dir.is_empty()) {
		warnings_.push_back(vformat("cannot make the directory for %s", p_path));
		return ERR_CANT_CREATE;
	}

	char text[REPLAY_HEADER_CHARS];
	const int length = replay_format_header(header_, text, REPLAY_HEADER_CHARS);
	if (length < 0) {
		warnings_.push_back(from_ascii("the header did not fit its own buffer"));
		return ERR_INVALID_DATA;
	}

	PackedByteArray out;
	// `String::utf8` rather than a bare assignment: the header is ASCII by
	// construction, but a track id arrives from a file and `godot::String` decodes
	// a bare `const char *` as Latin-1, so a non-ASCII byte in one would be
	// re-encoded into a different byte sequence than the one that was hashed.
	const CharString header_bytes = String::utf8(text, length).utf8();
	out.resize(header_bytes.length());
	for (int i = 0; i < header_bytes.length(); ++i) {
		out.set(i, static_cast<uint8_t>(header_bytes[i]));
	}
	const CharString marker = vformat("%s%d%s%d\n", from_ascii(BODY_MARKER), body_.size(),
			from_ascii(FOOTER_WORD), footer_.size())
									  .utf8();
	for (int i = 0; i < marker.length(); ++i) {
		out.push_back(static_cast<uint8_t>(marker[i]));
	}
	out.append_array(body_);
	out.append_array(footer_);

	// The temporary lives beside the target. `FileAccess::create_temp` would land
	// it in the OS temp dir, often another filesystem, where the rename fails
	// `EXDEV` -- and a replay body is megabytes, so a cross-device copy would not
	// be a cheap fallback either.
	return atomic_store(p_path, p_path + from_ascii(".tmp"), out, warnings_);
}

Error KartReplay::load(const String &p_path) {
	warnings_.clear();
	parse_ = ReplayParse();
	if (!FileAccess::file_exists(p_path)) {
		warnings_.push_back(vformat("%s does not exist", p_path));
		return ERR_FILE_NOT_FOUND;
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		warnings_.push_back(vformat("cannot read %s (error %d)", p_path,
				FileAccess::get_open_error()));
		return ERR_FILE_CANT_READ;
	}
	const int64_t size = static_cast<int64_t>(file->get_length());
	const PackedByteArray raw = file->get_buffer(size);
	file->close();
	if (raw.size() != size) {
		warnings_.push_back(vformat("%s is %d bytes and %d were read", p_path, size, raw.size()));
		return ERR_FILE_CANT_READ;
	}

	// Find the marker line. It is the last line of the text region and it carries
	// both lengths, which is what turns a truncated file into a length mismatch
	// rather than a body read at the wrong stride.
	const CharString needle = (String("\n") + from_ascii(BODY_MARKER)).utf8();
	int64_t marker_start = -1;
	for (int64_t i = 0; i + needle.length() <= raw.size(); ++i) {
		bool same = true;
		for (int j = 0; j < needle.length(); ++j) {
			if (raw[i + j] != static_cast<uint8_t>(needle[j])) {
				same = false;
				break;
			}
		}
		if (same) {
			marker_start = i + 1; // past the newline that belongs to the header
			break;
		}
	}
	if (marker_start < 0) {
		warnings_.push_back(vformat("%s has no body marker; it is not a replay", p_path));
		return ERR_FILE_UNRECOGNIZED;
	}
	int64_t marker_end = marker_start;
	while (marker_end < raw.size() && raw[marker_end] != '\n') {
		++marker_end;
	}
	if (marker_end >= raw.size()) {
		warnings_.push_back(from_ascii("the body marker line is not terminated"));
		return ERR_FILE_CORRUPT;
	}
	const int64_t text_end = marker_end + 1;

	String marker_line;
	{
		PackedByteArray slice = raw.slice(marker_start, marker_end);
		marker_line = slice.get_string_from_utf8();
	}
	const PackedStringArray fields = marker_line.split(" ", false);
	// "# body <n> footer <n>" is four tokens after the '#'.
	if (fields.size() != 5 || fields[1] != "body" || fields[3] != "footer") {
		warnings_.push_back(vformat("the body marker reads '%s'", marker_line));
		return ERR_FILE_CORRUPT;
	}
	if (!fields[2].is_valid_int() || !fields[4].is_valid_int()) {
		warnings_.push_back(vformat("the body marker's lengths are not numbers: '%s'",
				marker_line));
		return ERR_FILE_CORRUPT;
	}
	const int64_t body_bytes = fields[2].to_int();
	const int64_t footer_bytes = fields[4].to_int();
	if (body_bytes < 0 || footer_bytes < 0) {
		return ERR_FILE_CORRUPT;
	}

	ReplayHeader parsed;
	{
		const PackedByteArray slice = raw.slice(0, text_end);
		const String text = slice.get_string_from_utf8();
		const CharString utf8 = text.utf8();
		parse_ = replay_parse_header(utf8.get_data(), utf8.length(), parsed);
	}
	if (!parse_.ok) {
		warnings_.push_back(vformat("%s: line %d: %s%s", p_path, parse_.line,
				from_ascii(replay_parse_problem_name(parse_.problem)),
				parse_.key[0] == '\0' ? String() : vformat(" (%s)", from_ascii(parse_.key))));
		return ERR_FILE_CORRUPT;
	}

	// The declared lengths against the header's own arithmetic, before any of it is
	// indexed. `replay.h` computes both from `tick_count`, `entry_count` and
	// `hash_interval`, so a file whose marker disagrees with its header is corrupt
	// in a way that would otherwise show up as a plausible input stream.
	if (body_bytes != static_cast<int64_t>(parsed.body_bytes())) {
		warnings_.push_back(vformat("the body is %d bytes and the header declares %d ticks of "
									"%d karts, which is %d",
				body_bytes, static_cast<int64_t>(parsed.tick_count), parsed.kart_count(),
				static_cast<int64_t>(parsed.body_bytes())));
		return ERR_FILE_CORRUPT;
	}
	if (footer_bytes != static_cast<int64_t>(parsed.footer_bytes())) {
		warnings_.push_back(vformat("the footer is %d bytes and the header wants %d", footer_bytes,
				static_cast<int64_t>(parsed.footer_bytes())));
		return ERR_FILE_CORRUPT;
	}
	if (text_end + body_bytes + footer_bytes > raw.size()) {
		// Kept as its own sentence and its own return: a truncated body is
		// `RefusalReason::TruncatedBody` at playback, which is a refusal and not a
		// divergence.
		warnings_.push_back(vformat("%s is %d bytes and the header wants %d", p_path, raw.size(),
				text_end + body_bytes + footer_bytes));
		return ERR_FILE_EOF;
	}

	header_ = parsed;
	body_ = raw.slice(text_end, text_end + body_bytes);
	footer_ = raw.slice(text_end + body_bytes, text_end + body_bytes + footer_bytes);
	recorded_ticks_ = static_cast<int64_t>(header_.tick_count);
	recording_ = false;
	finalized_ = true;
	loaded_ = true;
	playing_ = false;
	report_ = PlaybackReport();
	return OK;
}

PackedStringArray KartReplay::warnings() const {
	return warnings_;
}

String KartReplay::parse_problem() const {
	return from_ascii(replay_parse_problem_name(parse_.problem));
}

int KartReplay::parse_line() const {
	return parse_.line;
}

String KartReplay::parse_key() const {
	return from_ascii(parse_.key);
}

// --- reading the header ------------------------------------------------------------

int KartReplay::get_format_version() const {
	return header_.format_version;
}

String KartReplay::get_build() const {
	return from_ascii(header_.build);
}

String KartReplay::get_api_version() const {
	return from_ascii(header_.api_version);
}

int64_t KartReplay::get_tick_count() const {
	return static_cast<int64_t>(header_.tick_count);
}

int KartReplay::get_kart_count() const {
	return header_.kart_count();
}

String KartReplay::config_hash_hex() const {
	return hex64(header_.config_hash);
}

Ref<KartSession> KartReplay::to_session() const {
	Ref<KartSession> session;
	session.instantiate();
	session->config() = header_.config;
	return session;
}

String KartReplay::header_text() const {
	char text[REPLAY_HEADER_CHARS];
	const int length = replay_format_header(header_, text, REPLAY_HEADER_CHARS);
	if (length < 0) {
		return String();
	}
	return String::utf8(text, length);
}

// --- reading the body and footer -----------------------------------------------------

Dictionary KartReplay::input_at(int64_t p_tick, int p_kart) const {
	const int karts = header_.kart_count();
	if (p_tick < 0 || p_tick >= recorded_ticks_ || p_kart < 0 || p_kart >= karts) {
		return Dictionary();
	}
	const int64_t offset = (p_tick * karts + p_kart) * REPLAY_INPUT_BYTES;
	if (offset + REPLAY_INPUT_BYTES > body_.size()) {
		return Dictionary();
	}
	unsigned char scratch[REPLAY_INPUT_BYTES];
	for (int byte = 0; byte < REPLAY_INPUT_BYTES; ++byte) {
		scratch[byte] = body_[offset + byte];
	}
	DriverInput input;
	if (!replay_decode_input(scratch, REPLAY_INPUT_BYTES, input)) {
		// A reserved flag bit, or a code the encoder cannot emit. Empty rather than
		// a defaulted struct: neutral input is a plausible tick and this is not one.
		return Dictionary();
	}
	return dictionary_from_input(input);
}

int64_t KartReplay::checkpoint_count() const {
	return footer_.size() / REPLAY_CHECKPOINT_BYTES;
}

int64_t KartReplay::checkpoint_tick(int64_t p_index) const {
	if (p_index < 0 || p_index >= checkpoint_count()) {
		return -1;
	}
	unsigned char scratch[REPLAY_CHECKPOINT_BYTES];
	for (int byte = 0; byte < REPLAY_CHECKPOINT_BYTES; ++byte) {
		scratch[byte] = footer_[p_index * REPLAY_CHECKPOINT_BYTES + byte];
	}
	ReplayCheckpoint point;
	if (!replay_decode_checkpoint(scratch, REPLAY_CHECKPOINT_BYTES, point)) {
		return -1;
	}
	return static_cast<int64_t>(point.tick);
}

String KartReplay::checkpoint_hash_hex(int64_t p_index) const {
	if (p_index < 0 || p_index >= checkpoint_count()) {
		return String();
	}
	unsigned char scratch[REPLAY_CHECKPOINT_BYTES];
	for (int byte = 0; byte < REPLAY_CHECKPOINT_BYTES; ++byte) {
		scratch[byte] = footer_[p_index * REPLAY_CHECKPOINT_BYTES + byte];
	}
	ReplayCheckpoint point;
	if (!replay_decode_checkpoint(scratch, REPLAY_CHECKPOINT_BYTES, point)) {
		return String();
	}
	return hex64(point.hash);
}

String KartReplay::checkpoint_hash_at(int64_t p_tick) const {
	if (!is_checkpoint_tick(p_tick)) {
		return String();
	}
	const int64_t index = p_tick / header_.hash_interval;
	if (checkpoint_tick(index) != p_tick) {
		return String();
	}
	return checkpoint_hash_hex(index);
}

// --- playback -------------------------------------------------------------------------

bool KartReplay::live_header(const Ref<KartSession> &p_live, ReplayHeader &out) const {
	if (p_live.is_null()) {
		return false;
	}
	out = ReplayHeader();
	out.format_version = REPLAY_FORMAT_VERSION;
	out.hash_interval = header_.hash_interval;
	out.tick_count = header_.tick_count;
	out.config = p_live->config();
	out.set_build(this_build().utf8().get_data());
	out.set_api_version(this_api_version().utf8().get_data());
	out.stamp();
	return true;
}

bool KartReplay::begin_playback(const Ref<KartSession> &p_live) {
	report_ = PlaybackReport();
	playing_ = false;
	if (!live_header(p_live, live_header_)) {
		ERR_PRINT("KartReplay.begin_playback: no live session");
		return false;
	}
	report_ = replay_admit(header_, live_header_);
	// The body length is checked separately, because the header and the body arrive
	// from this side separately and a truncated body is worth its own sentence.
	// Only when the header was admitted: a refusal already has a reason and
	// overwriting it would lose the one that came first.
	if (report_.admitted() &&
			!replay_body_is_complete(header_, static_cast<uint64_t>(body_.size()))) {
		replay_refuse_truncated(report_);
	}
	playing_ = report_.admitted();
	return playing_;
}

void KartReplay::compare_checkpoint(int64_t p_tick, const String &p_live_hash_hex) {
	if (p_tick < 0) {
		return;
	}
	const String recorded = checkpoint_hash_at(p_tick);
	if (recorded.is_empty()) {
		// No recorded checkpoint at this tick. Silently skipping it would let a
		// harness compare nothing and report a pass, so this is reported the way
		// `replay_compare_footer` reports a misalignment -- as a divergence whose
		// live hash is deliberately the complement, which cannot be mistaken for a
		// real one.
		uint64_t live = 0;
		parse_hex64(p_live_hash_hex, live);
		replay_compare_checkpoint(report_, static_cast<uint64_t>(p_tick), ~live, live);
		return;
	}
	uint64_t recorded_digest = 0;
	uint64_t live_digest = 0;
	if (!parse_hex64(recorded, recorded_digest) || !parse_hex64(p_live_hash_hex, live_digest)) {
		return;
	}
	replay_compare_checkpoint(report_, static_cast<uint64_t>(p_tick), recorded_digest,
			live_digest);
}

void KartReplay::compare_footer(const PackedStringArray &p_live_hashes) {
	const int64_t count = checkpoint_count();
	for (int64_t i = 0; i < count && i < p_live_hashes.size(); ++i) {
		compare_checkpoint(checkpoint_tick(i), p_live_hashes[i]);
	}
	if (p_live_hashes.size() != count && report_.admitted()) {
		// Not a divergence: the two runs did not produce the same number of
		// checkpoints, which is a harness fault and gets a warning rather than a
		// verdict it did not earn.
		warnings_.push_back(vformat("%d live checkpoints against %d recorded",
				p_live_hashes.size(), count));
	}
}

int KartReplay::verdict() const {
	return static_cast<int>(report_.verdict);
}

String KartReplay::verdict_name() const {
	return from_ascii(playback_verdict_name(report_.verdict));
}

int KartReplay::refusal_reason() const {
	return static_cast<int>(report_.reason);
}

String KartReplay::refusal_name() const {
	return from_ascii(refusal_reason_name(report_.reason));
}

String KartReplay::describe() const {
	char text[REPLAY_MESSAGE_CHARS];
	const int length = replay_describe(report_, header_, live_header_, text, REPLAY_MESSAGE_CHARS);
	if (length < 0) {
		return from_ascii("the verdict did not fit its own buffer, which is itself a bug");
	}
	return String::utf8(text, length);
}

int64_t KartReplay::diverged_tick() const {
	return static_cast<int64_t>(report_.diverged_tick);
}

String KartReplay::recorded_hash_hex() const {
	return hex64(report_.recorded_hash);
}

String KartReplay::live_hash_hex() const {
	return hex64(report_.live_hash);
}

int KartReplay::checkpoints_compared() const {
	return report_.checkpoints_compared;
}

bool KartReplay::build_warning() const {
	return report_.build_warning;
}

bool KartReplay::api_warning() const {
	return report_.api_warning;
}

// --- deliberate sabotage ----------------------------------------------------------------

bool KartReplay::perturb_input(int64_t p_tick, int p_kart, int p_axis) {
	const int karts = header_.kart_count();
	if (p_tick < 0 || p_tick >= recorded_ticks_ || p_kart < 0 || p_kart >= karts) {
		return false;
	}
	if (p_axis < 0 || p_axis > 3) {
		return false;
	}
	const int64_t offset = (p_tick * karts + p_kart) * REPLAY_INPUT_BYTES + p_axis * 2;
	if (offset + 2 > body_.size()) {
		return false;
	}
	// One code, which is the smallest change the format can express: 1.53e-5 of
	// throttle, brake or clutch, 3.05e-5 of lock. Moved *down* when the axis is
	// already at its maximum, so that a saboteur cannot silently clamp back to the
	// value it was trying to change -- a negative control that changes nothing
	// reports "caught" off somebody else's red.
	uint16_t code = static_cast<uint16_t>(body_[offset] | (body_[offset + 1] << 8));
	if (p_axis == 3) {
		int16_t steer = static_cast<int16_t>(code);
		steer = steer >= REPLAY_STEER_CODES ? static_cast<int16_t>(steer - 1)
										    : static_cast<int16_t>(steer + 1);
		code = static_cast<uint16_t>(steer);
	} else {
		code = code >= REPLAY_UNIT_CODES ? static_cast<uint16_t>(code - 1)
										 : static_cast<uint16_t>(code + 1);
	}
	body_.set(offset, static_cast<uint8_t>(code & 0xFFU));
	body_.set(offset + 1, static_cast<uint8_t>((code >> 8) & 0xFFU));
	return true;
}

bool KartReplay::truncate_body(int64_t p_bytes) {
	if (p_bytes <= 0 || p_bytes > body_.size()) {
		return false;
	}
	body_ = body_.slice(0, body_.size() - p_bytes);
	return true;
}

} // namespace kartgame
