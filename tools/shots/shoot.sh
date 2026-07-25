#!/usr/bin/env bash
#
# Render one look-dev still.
#
#     tools/shots/shoot.sh --scene=res://scenes/look/lookdev.tscn \
#                          --out=shots/asphalt.png \
#                          --speed=80
#
# Every argument is forwarded verbatim to the scene, so what a still shows is
# fully described by the command that produced it. Recognized here rather than
# forwarded: --godot, --resolution, --settle, --out.
#
# A window opens for the duration. That is not avoidable: --headless has no
# rendering device, so there are no pixels to save. See tools/shots/shoot.gd.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
RESOLUTION="1920x1080"
OUT="shots/shot.png"
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--godot=*)      GODOT="${argument#*=}" ;;
		--resolution=*) RESOLUTION="${argument#*=}" ;;
		--out=*)        OUT="${argument#*=}" ;;
		*)              FORWARDED+=("$argument") ;;
	esac
done

# save_png() resolves a bare path against the process working directory, which is
# not the project root. Absolutize here so a still lands where the caller meant.
case "$OUT" in
	/*) ABS_OUT="$OUT" ;;
	*)  ABS_OUT="$PROJECT_ROOT/$OUT" ;;
esac

# Stills written under the project would otherwise be picked up by Godot's
# importer on the next run, which both slows the loop down and litters .import
# files next to the output. The directory is gitignored, so the marker has to be
# created here rather than committed.
mkdir -p "$(dirname "$ABS_OUT")"
touch "$(dirname "$ABS_OUT")/.gdignore"

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	exit 127
fi

# Running a project outside the editor does not import resources, so a freshly
# added .glsl or texture is simply missing. Import first — and tolerate the cold
# pass crashing, which is the macOS MoltenVK defect in ADR-0018 and not a
# failure of anything here.
#
# SKIP_IMPORT=1 drops about four seconds from a shot. Safe only while iterating
# on code, which is not imported; adding or changing any asset needs the import.
if [ "${SKIP_IMPORT:-0}" != "1" ]; then
	"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
	if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
		echo "error: the warm import failed, which is not the known macOS bug" >&2
		exit 1
	fi
fi

exec "$GODOT" \
	--path "$PROJECT_ROOT" \
	--resolution "$RESOLUTION" \
	--audio-driver Dummy \
	--script res://tools/shots/shoot.gd \
	-- \
	--out="$ABS_OUT" \
	${FORWARDED[@]+"${FORWARDED[@]}"}
