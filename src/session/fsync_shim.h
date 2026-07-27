#ifndef KARTGAME_SESSION_FSYNC_SHIM_H
#define KARTGAME_SESSION_FSYNC_SHIM_H

#include <godot_cpp/variant/string.hpp>

// Issue #173: Godot's `FileAccess` cannot sync. 68 methods and none of them
// reach the disk — no `fsync`, no `F_FULLFSYNC`, no `O_SYNC`, nothing in
// `DirAccess` either, checked against `extension_api.json` rather than
// remembered. So the atomic writes in `kart_profile.cpp` and
// `tuning_registry.cpp` bought process death and not a power cut, while two
// ADRs instructed an fsync nobody could perform. This is the shim those ADRs
// were owed: the one place in the project that opens a path behind the
// engine's back, and it does nothing else with it.
//
// The platform calls, named per platform as the ticket requires:
//
//   * macOS: `fcntl(fd, F_FULLFSYNC)`. A plain `fsync` on Darwin flushes to
//     the drive, not through its cache, and Apple's own manpage says so.
//     Falls back to `fsync` where a filesystem refuses F_FULLFSYNC.
//   * everything else POSIX: `fsync(fd)`.
//   * Windows, should an export ever compile this: `_commit(fd)`.
//
// What the two calls buy, stated exactly:
//
//   * `fsync_file(temp)` **between the close and the rename** makes the new
//     bytes durable before any name points at them. A power cut anywhere in
//     the sequence now leaves either the old complete file or the new
//     complete file — never a correctly named empty one, which is the exact
//     failure `kart_profile.h` documented as open.
//   * `fsync_dir(directory)` **after the rename** makes the rename itself
//     durable, so a save that reported OK still exists after the cut. Without
//     it a journaled filesystem may legally revert to the old file, which
//     loses a lap, not a career — that is why the callers treat a directory
//     sync failure as a warning and a file sync failure as a failed save.

namespace kartgame {

// Sync a file's bytes to the storage medium. Takes a Godot path (`user://...`
// or absolute) and globalizes it internally. Returns:
//
//   * `godot::OK` — the platform call succeeded.
//   * `godot::ERR_UNAVAILABLE` — the filesystem refused the operation
//     (EINVAL/ENOTSUP: some network and FUSE mounts). Nothing stronger exists
//     there; callers warn and keep the process-death guarantee they had.
//   * `godot::ERR_FILE_CANT_WRITE` — the sync genuinely failed (EIO). The
//     bytes may not be on disk and a caller claiming durability must not.
godot::Error fsync_file(const godot::String &p_path);

// Sync a directory, making a completed rename durable. Same returns.
godot::Error fsync_dir(const godot::String &p_path);

} // namespace kartgame

#endif // KARTGAME_SESSION_FSYNC_SHIM_H
