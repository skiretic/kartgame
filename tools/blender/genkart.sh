#!/usr/bin/env bash
#
# Generate the kart mesh.
#
#     tools/blender/genkart.sh                          # assets/generated/kart.glb
#     tools/blender/genkart.sh --out=/tmp/k.glb
#     tools/blender/genkart.sh --stages=geometry,export  # skip the bake
#     tools/blender/genkart.sh --set=track_rear=1.38     # parameter sweep
#     tools/blender/genkart.sh --check                   # determinism gate
#
# Every argument is forwarded verbatim to genkart.py, so what a mesh contains is
# fully described by the command that produced it -- the same rule tools/shots/
# shoot.sh holds for stills. Recognized here rather than forwarded: --blender,
# --check, --quiet.
#
# --check is the M2 acceptance gate's determinism half. It generates the kart
# twice into a scratch directory and compares SHA-256. Determinism is part of the
# gate rather than a bonus, so it gets a first-class flag instead of living in
# somebody's shell history.
#
# Blender is pinned to 5.2 LTS: bpy shifts between major versions and a 5.x
# script will not run unmodified on 4.x. See ADR-0012. The version is checked
# rather than assumed, because the failure mode otherwise is a confusing
# AttributeError several hundred lines into a build.

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
		--quiet)     QUIET=1 ;;
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
	# disables add-ons, and Cycles is an add-on that the normal bake needs. The
	# script resets the scene from factory settings itself, which discards the
	# user's startup file without unloading anything.
	"$BLENDER" --background --python "$PROJECT_ROOT/tools/blender/genkart.py" -- "$@"
}

if [ "$CHECK" = "1" ]; then
	# --check owns the output path, because it needs two of them to compare. If the
	# caller also passed --out it wins inside genkart.py (later argument), so both
	# passes would write to the caller's file while cmp looked in the temp
	# directory -- reporting a determinism failure that is purely an argument
	# collision. Rejected rather than silently ignored: someone combining these
	# two flags means something, and guessing which is worse than asking.
	for argument in ${FORWARDED[@]+"${FORWARDED[@]}"}; do
		case "$argument" in
			--out|--out=*)
				echo "error: --check cannot be combined with --out." >&2
				echo "       --check generates twice into a temporary directory and" >&2
				echo "       compares the two, so it chooses its own output paths." >&2
				exit 2
				;;
		esac
	done

	SCRATCH="$(mktemp -d)"
	trap 'rm -rf "$SCRATCH"' EXIT

	# Each pass writes into its own directory, because the mesh is not the only
	# output: the normal bake drops kart_normal.png beside it, and with one shared
	# directory pass 2 would overwrite pass 1's and the texture would go unchecked.
	# The bake is a Monte Carlo render, so it is the output most likely to be
	# non-reproducible and the least excusable to leave uncompared.
	echo "==> Determinism check: generating twice"
	for pass in 1 2; do
		mkdir -p "$SCRATCH/pass$pass"
		if ! run_blender --out="$SCRATCH/pass$pass/kart.glb" \
			${FORWARDED[@]+"${FORWARDED[@]}"} >"$SCRATCH/log$pass.txt" 2>&1
		then
			echo "error: pass $pass failed" >&2
			tail -30 "$SCRATCH/log$pass.txt" >&2
			exit 1
		fi
		printf '    pass %d  %s\n' "$pass" \
			"$(shasum -a 256 "$SCRATCH/pass$pass/kart.glb" | awk '{print $1}')"
	done

	FAILED=0
	for artifact in kart.glb kart_normal.png; do
		if [ ! -e "$SCRATCH/pass1/$artifact" ]; then
			continue
		fi
		if cmp -s "$SCRATCH/pass1/$artifact" "$SCRATCH/pass2/$artifact"; then
			printf '    %-16s identical\n' "$artifact"
		else
			printf '    %-16s DIFFERS\n' "$artifact" >&2
			FAILED=1
		fi
	done

	if [ "$FAILED" = "1" ]; then
		echo "error: the two runs differ, so the output is not reproducible." >&2
		echo "       Look for randomness, wall-clock time, a set/dict iteration," >&2
		echo "       or a generated object name with a counter in it. For the" >&2
		echo "       normal map specifically, check the Cycles seed, sample count," >&2
		echo "       and that denoising is still off." >&2
		exit 1
	fi
	echo "==> Identical. Determinism holds."

	# The manifests are text, so when the binaries differ this is the readable
	# half of the diff. Compared second because a hash mismatch is the failure
	# and this only explains it.
	#
	# The `gltf` field is excluded: the two passes are deliberately written to
	# different filenames, so it is the one field that is *supposed* to differ.
	# Everything else in the manifest must not -- a wall-clock build duration lived
	# in there at first and this check is what caught it.
	strip_output_path() { grep -v '"gltf":' "$1"; }
	if ! diff -u <(strip_output_path "$SCRATCH/pass1/kart.json") \
	             <(strip_output_path "$SCRATCH/pass2/kart.json") >/dev/null 2>&1
	then
		echo "warning: manifests differ even though the meshes matched" >&2
		diff -u <(strip_output_path "$SCRATCH/pass1/kart.json") \
		        <(strip_output_path "$SCRATCH/pass2/kart.json") | head -40 >&2
	fi
	exit 0
fi

if [ "$QUIET" = "1" ]; then
	run_blender ${FORWARDED[@]+"${FORWARDED[@]}"} >/dev/null 2>&1
	exit $?
fi

# Blender writes its splash and add-on chatter to stdout before the script runs.
# Dropping it keeps the useful output -- the stage list, counts and hash -- from
# scrolling away, while anything on stderr still comes through.
run_blender ${FORWARDED[@]+"${FORWARDED[@]}"} | grep -vE '(^Blender |^Read prefs|^found bundled|^Warning: |^Writing |INFO: |^Info: )'
exit "${PIPESTATUS[0]}"
