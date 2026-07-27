#include "session/fsync_shim.h"

#include <godot_cpp/classes/project_settings.hpp>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace godot;

namespace kartgame {

namespace {

// `user://` to an OS path. `ProjectSettings` is the only resolver of the
// prefix; an absolute path passes through unchanged.
String os_path(const String &p_path) {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr) {
		return p_path;
	}
	return settings->globalize_path(p_path);
}

#ifndef _WIN32

Error sync_fd(int fd) {
#ifdef __APPLE__
	// F_FULLFSYNC or the drive cache eats the guarantee. Falls back to fsync
	// where the filesystem refuses the fcntl — still stronger than nothing,
	// and the fallback's own failure is still reported.
	if (fcntl(fd, F_FULLFSYNC) == 0) {
		return OK;
	}
	if (errno != EINVAL && errno != ENOTSUP) {
		return ERR_FILE_CANT_WRITE;
	}
#endif
	if (fsync(fd) == 0) {
		return OK;
	}
	if (errno == EINVAL || errno == ENOTSUP) {
		return ERR_UNAVAILABLE;
	}
	return ERR_FILE_CANT_WRITE;
}

Error sync_path(const String &p_path, int open_flags) {
	const CharString utf8 = os_path(p_path).utf8();
	const int fd = ::open(utf8.get_data(), open_flags);
	if (fd < 0) {
		return ERR_FILE_CANT_OPEN;
	}
	const Error result = sync_fd(fd);
	::close(fd);
	return result;
}

#endif // !_WIN32

} // namespace

Error fsync_file(const String &p_path) {
#ifdef _WIN32
	const CharString utf8 = os_path(p_path).utf8();
	const int fd = ::_open(utf8.get_data(), _O_RDONLY | _O_BINARY);
	if (fd < 0) {
		return ERR_FILE_CANT_OPEN;
	}
	const Error result = ::_commit(fd) == 0 ? OK : ERR_FILE_CANT_WRITE;
	::_close(fd);
	return result;
#else
	return sync_path(p_path, O_RDONLY);
#endif
}

Error fsync_dir(const String &p_path) {
#ifdef _WIN32
	// Windows has no directory sync; the rename's durability rides on the
	// filesystem. Reported as unavailable rather than pretended.
	return ERR_UNAVAILABLE;
#else
	return sync_path(p_path, O_RDONLY | O_DIRECTORY);
#endif
}

} // namespace kartgame
