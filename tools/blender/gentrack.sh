#!/usr/bin/env bash
#
# Generate a circuit's visual mesh from track.json.
#
#     tools/blender/gentrack.sh                       # assets/generated/<name>.glb
#     tools/blender/gentrack.sh --track=data/tracks/other.track.json
#     tools/blender/gentrack.sh --out=/tmp/t.glb
#     tools/blender/gentrack.sh --stages=geometry,uv,manifest   # skip the export
#     tools/blender/gentrack.sh --check                # determinism gate
#     tools/blender/gentrack.sh --blend=/tmp/t.blend   # dump a .blend to inspect
#
# Every argument is forwarded verbatim to gentrack.py, so what a mesh contains is
# fully described by the command that produced it -- the same rule genkart.sh and
# tools/shots/shoot.sh hold. Recognized here rather than forwarded: --blender,
# --check, --quiet.
#
# --check is the determinism half of the M5 gate. It generates the circuit twice
# into a scratch directory and compares SHA-256 of both the .glb and the manifest.
# Determinism is an acceptance gate item at M2, again at M3a and again here, not a
# bonus, so it gets a first-class flag instead of living in somebody's shell
# history.
#
# Blender is pinned to 5.2 LTS: bpy shifts between major versions. See ADR-0012.
#
# The geometry, UV and manifest stages need no bpy at all -- tracklib/geometry.py
# deliberately does not import it -- so on a machine with no Blender:
#
#     python3 tools/blender/gentrack.py --stages=geometry,uv,manifest
#
# still produces the manifest that tools/verify/circuit.sh cross-checks the
# collider against. Only the .glb needs Blender.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

BLENDER="${BLENDER:-}"
CHECK=0
QUIET=0
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--blender=*) BLENDER="${argument#*=}" ;;
		--check)     CHECK=1 ;;
		--quiet)     QUIET=1 ; FORWARDED+=("$argument") ;;
		*)           FORWARDED+=("$argument") ;;
	esac
done

# Blender does not install itself onto the PATH on macOS, so the .app is checked
# before giving up. Linux and Windows package it normally.
if [ -z "$BLENDER" ]; then
	for candidate in \
		blender \
		/Applications/Blender.app/Contents/MacOS/Blender \
		"$HOME/Applications/Blender.app/Contents/MacOS/Blender"
	do
		if command -v "$candidate" >/dev/null 2>&1 || [ -x "$candidate" ]; then
			BLENDER="$candidate"
			break
		fi
	done
fi

if [ -z "$BLENDER" ]; then
	echo "error: Blender not found. Pass --blender=/path/to/blender or set BLENDER." >&2
	echo "       The geometry and manifest stages need no Blender:" >&2
	echo "       python3 tools/blender/gentrack.py --stages=geometry,uv,manifest" >&2
	exit 127
fi

VERSION="$("$BLENDER" --version 2>/dev/null | head -1 | awk '{print $2}')"
case "$VERSION" in
	5.2*) ;;
	"")
		echo "error: could not read a version from '$BLENDER'" >&2
		exit 1
		;;
	*)
		echo "warning: Blender $VERSION found, but this pipeline targets 5.2 LTS." >&2
		echo "         bpy shifts between major versions -- see ADR-0012." >&2
		;;
esac

run_blender() {
	# --background is headless. --factory-startup is deliberately NOT passed: it
	# disables add-ons, and gentrack.py resets the scene from inside instead, which
	# discards the user's startup file without unloading anything.
	"$BLENDER" --background --python "$PROJECT_ROOT/tools/blender/gentrack.py" -- "$@"
}

if [ "$CHECK" = "1" ]; then
	# --check owns the output paths, because it needs two of each to compare. If
	# the caller also passed --out or --manifest, theirs would win inside
	# gentrack.py (later argument) and both passes would write to the caller's file
	# while cmp looked in the scratch directory -- reporting a determinism failure
	# that is purely an argument collision. Rejected rather than silently ignored.
	for argument in ${FORWARDED[@]+"${FORWARDED[@]}"}; do
		case "$argument" in
			--out|--out=*|--manifest|--manifest=*)
				echo "error: --check cannot be combined with --out or --manifest." >&2
				echo "       It generates twice into a scratch directory and compares," >&2
				echo "       so it chooses its own output paths." >&2
				exit 2
				;;
		esac
	done

	SCRATCH="$(mktemp -d)"
	trap 'rm -rf "$SCRATCH"' EXIT

	echo "==> Determinism check: generating twice"
	for pass in 1 2; do
		mkdir -p "$SCRATCH/pass$pass"
		if ! run_blender --out="$SCRATCH/pass$pass/track.glb" \
			--manifest="$SCRATCH/pass$pass/track.manifest.json" \
			${FORWARDED[@]+"${FORWARDED[@]}"} >"$SCRATCH/log$pass.txt" 2>&1
		then
			echo "error: pass $pass failed" >&2
			tail -30 "$SCRATCH/log$pass.txt" >&2
			exit 1
		fi
		printf '    pass %d  %s\n' "$pass" \
			"$(shasum -a 256 "$SCRATCH/pass$pass/track.glb" | awk '{print $1}')"
	done

	FAILED=0
	for artifact in track.glb track.manifest.json; do
		if [ ! -e "$SCRATCH/pass1/$artifact" ]; then
			continue
		fi
		if cmp -s "$SCRATCH/pass1/$artifact" "$SCRATCH/pass2/$artifact"; then
			printf '    %-22s identical\n' "$artifact"
		else
			printf '    %-22s DIFFERS\n' "$artifact" >&2
			FAILED=1
		fi
	done

	if [ "$FAILED" = "1" ]; then
		echo "error: the two runs differ, so the output is not reproducible." >&2
		echo "       Look for randomness, wall-clock time, a set or dict iteration," >&2
		echo "       or a name that is not fixed. See gentrack.py's header." >&2
		exit 1
	fi
	echo "==> Deterministic."
	exit 0
fi

if [ "$QUIET" = "1" ]; then
	run_blender ${FORWARDED[@]+"${FORWARDED[@]}"} >/dev/null 2>&1
	exit $?
fi

run_blender ${FORWARDED[@]+"${FORWARDED[@]}"}
