#!/usr/bin/env bash
#
# Build and run the C++ unit tests over src/core/.
#
#     tests/run.sh                 # build if needed, then run
#     tests/run.sh --clean         # rebuild from scratch
#     tests/run.sh -ts=pcg32       # any doctest flag is forwarded
#
# No Godot, no godot-cpp, no generated bindings, no engine. That is the whole
# point: ADR-0017 keeps `src/core/` free of godot-cpp so the vehicle math, the
# tire curves and the spline solver can be tested as ordinary C++, and issue #25
# is where that promise is cashed in.
#
# **The boundary is enforced, not trusted.** The compile below has exactly one
# include path — `src/` — so a header that reached for godot-cpp would fail to
# compile rather than quietly pull the engine into the test binary. A grep check
# runs first anyway, because the failure mode without it is a confusing
# "file not found" several hundred lines into a template instantiation.
#
# Builds with the system compiler rather than through SCons. SCons here would
# mean sourcing godot-cpp's SConstruct, which builds the bindings — a two-minute
# dependency for a test suite whose selling point is that it runs in seconds.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/bin/tests"
BINARY="$BUILD_DIR/core_tests"
DOCTEST="$PROJECT_ROOT/third_party/doctest/doctest.h"

CLEAN=0
FORWARDED=()
for argument in "$@"; do
	case "$argument" in
		--clean) CLEAN=1 ;;
		*)       FORWARDED+=("$argument") ;;
	esac
done

if [ ! -f "$DOCTEST" ]; then
	echo "==> doctest not present; fetching"
	"$PROJECT_ROOT/tools/assets/fetch_doctest.sh" || exit 1
fi

# ADR-0017's boundary, checked before the compiler gets a chance to. Anything
# under src/core/ that includes godot-cpp has broken the one rule that makes this
# suite possible, and the error should say so in those words.
# Restricted to source files by name. Grepping the whole directory matched this
# script's own comment about not including godot-cpp, which is a good joke and a
# bad check.
OFFENDERS="$(find "$PROJECT_ROOT/src/core" "$PROJECT_ROOT/tests" \
	-type f \( -name '*.h' -o -name '*.cpp' \) \
	-exec grep -lE '#include [<"]godot_cpp/' {} + 2>/dev/null)"
if [ -n "$OFFENDERS" ]; then
	echo "error: these files include godot-cpp, which src/core/ may not (ADR-0017):" >&2
	echo "$OFFENDERS" | sed 's/^/    /' >&2
	exit 1
fi

mkdir -p "$BUILD_DIR"

SOURCES=("$PROJECT_ROOT/tests/main.cpp")
while IFS= read -r file; do
	SOURCES+=("$file")
done < <(find "$PROJECT_ROOT/tests" -name 'test_*.cpp' | sort)

NEEDS_BUILD=0
if [ "$CLEAN" = "1" ] || [ ! -x "$BINARY" ]; then
	NEEDS_BUILD=1
else
	# Rebuild if any source or header is newer than the binary. Cheap, and it
	# means the common case — running the tests twice in a row — is instant.
	while IFS= read -r file; do
		if [ "$file" -nt "$BINARY" ]; then
			NEEDS_BUILD=1
			break
		fi
	done < <(find "$PROJECT_ROOT/tests" "$PROJECT_ROOT/src/core" -type f \( -name '*.cpp' -o -name '*.h' \))
fi

if [ "$NEEDS_BUILD" = "1" ]; then
	echo "==> Building ${#SOURCES[@]} translation unit(s)"
	# -I src/ and -I third_party/doctest only. godot-cpp is not on the path and
	# must not be added: the moment it is, this stops being a test of engine-free
	# code and starts being a slower, worse version of verify_extension.gd.
	if ! ${CXX:-c++} -std=c++17 -O1 -g \
		-Wall -Wextra -Wpedantic \
		-I "$PROJECT_ROOT/src" \
		-I "$PROJECT_ROOT/third_party/doctest" \
		"${SOURCES[@]}" -o "$BINARY"
	then
		echo "error: the test binary did not build" >&2
		exit 1
	fi
fi

exec "$BINARY" ${FORWARDED[@]+"${FORWARDED[@]}"}
