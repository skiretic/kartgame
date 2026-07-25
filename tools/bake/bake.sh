#!/usr/bin/env bash
#
# Bake the lightmap for a scene.
#
#     tools/bake/bake.sh --scene=res://scenes/look/bake_test.tscn
#     tools/bake/bake.sh --preflight-only
#
# Two steps, and only the first of them is really scripting.
#
# **Preflight** runs headless and asserts that the scene can be baked at all —
# UV2, normals, gi_mode, and the projected atlas size per mesh. Everything that
# makes a bake fail can be established without a GPU, and doing it here means a
# failure names the offending node instead of arriving as LightmapGI's single
# undifferentiated "no meshes to bake".
#
# **The bake** opens the GUI editor, because in Godot 4.7 there is no other way.
# `LightmapGI.bake()` is not bound to ClassDB, so no script of any kind can call
# it, and there is no command-line equivalent — the standalone editor tools are
# --import, --export-*, --doctool and nothing else. tools/bake/editor_bake.gd
# opens the scene, reaches into the editor's own bake plugin, drives it, saves,
# and quits. A window opens for the duration and the editor closes itself when
# the bake lands.
#
# To do it by hand instead — worth knowing, because the automation drives editor
# internals and those are allowed to move:
#
#   1. godot --path . --editor
#   2. Open scenes/look/bake_test.tscn. The geometry appears in the viewport;
#      it is built by a @tool script, and it has to be, because LightmapGI can
#      only bake what the editor can see.
#   3. Select the LightmapGI node in the scene tree. A "Bake Lightmaps" button
#      appears in the toolbar above the 3D viewport.
#   4. Press it. Accept the suggested path, scenes/look/bake_test.lmbake.
#   5. Save the scene — the bake writes its result to the node's `light_data`
#      property, and without a save nothing persists.
#
# Two engine messages show up on every successful run on this host and neither
# means anything is wrong:
#
#   ERROR: timeout waiting for fence   (drivers/metal/...)
#       The Metal backend's fence wait expires during a long compute dispatch.
#       It appears at ultra quality and not at high, and the bake completes and
#       is correct either way. Same family as ADR-0018 — a macOS backend defect,
#       not a project one. It is a warning sign for M5 though: a bake long enough
#       to trip a driver watchdog is a bake that can be killed by one.
#
#   WARNING: Scan thread aborted...
#       The editor's filesystem scanner being interrupted because we quit as
#       soon as the save lands. Cosmetic.
#
# Recognized here rather than forwarded: --godot, --scene, --preflight-only.
# Everything else goes to the scene, so bake parameters are set the same way
# every other look-dev parameter in this project is.

set -uo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

GODOT="${GODOT:-godot}"
SCENE="res://scenes/look/bake_test.tscn"
PREFLIGHT_ONLY=0
FORWARDED=()

for argument in "$@"; do
	case "$argument" in
		--godot=*)        GODOT="${argument#*=}" ;;
		--scene=*)        SCENE="${argument#*=}" ;;
		--preflight-only) PREFLIGHT_ONLY=1 ;;
		*)                FORWARDED+=("$argument") ;;
	esac
done

if ! command -v "$GODOT" >/dev/null 2>&1 && [ ! -x "$GODOT" ]; then
	echo "error: Godot not found at '$GODOT'" >&2
	exit 127
fi

# The cold headless import segfaults on macOS and seeds .godot/ before it dies,
# so the warm one is clean. ADR-0018; not a failure of anything here.
if [ "${SKIP_IMPORT:-0}" != "1" ]; then
	"$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1 || true
	if ! "$GODOT" --headless --path "$PROJECT_ROOT" --import >/dev/null 2>&1; then
		echo "error: the warm import failed, which is not the known macOS bug" >&2
		exit 1
	fi
fi

"$GODOT" --headless --path "$PROJECT_ROOT" \
	--script res://tools/bake/preflight.gd \
	-- --scene="$SCENE" "${FORWARDED[@]+"${FORWARDED[@]}"}"
preflight_status=$?

if [ "$preflight_status" -ne 0 ]; then
	echo "error: preflight failed, not baking" >&2
	exit "$preflight_status"
fi

if [ "$PREFLIGHT_ONLY" = "1" ]; then
	exit 0
fi

# No --quit-after here on purpose. The bake blocks the main loop while pumping
# the editor's progress dialog, so an iteration limit can fire in the middle of
# it. editor_bake.gd quits the editor itself when it is done.
exec "$GODOT" \
	--editor \
	--path "$PROJECT_ROOT" \
	"$SCENE" \
	--audio-driver Dummy \
	-- \
	--bake=true \
	"${FORWARDED[@]+"${FORWARDED[@]}"}"
