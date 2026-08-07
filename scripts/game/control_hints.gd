class_name ControlHints
extends RefCounted

## The on-screen control list, for whichever device is actually attached.
##
## ## Why this file exists
##
## The HUD's help line was keyboard-only, in both scenes, since M3a. A driver on
## a controller was shown "W/S throttle-brake  A/D steer  E/Q shift up-down" and
## nothing else, and the pad bindings existed only in `project.godot` and in
## CLAUDE.md — neither of which is visible while driving. The reported outcome
## was exactly what that predicts: *"i didn't try it i had no idea what to press.
## i closed it."*
##
## That is the same class as the two found earlier the same day — `look_back`
## printed and read by nothing, and `auto_shift` with no way to turn it off. All
## three are invisible to every headless gate, because gates drive through
## `KartBody.input_driver` and never touch the input map at all.
##
## ## And it names the pad, which is not decoration
##
## A DualSense over Bluetooth that has not paired presents identically to a driver
## pressing the wrong button: nothing happens. Printing the detected device's name
## makes those two different observations, and the second line below says so when
## there is nothing attached.
##
## ## One owner, because this list has drifted before
##
## `proving_ground.gd` and `test_track.gd` each held their own copy. CLAUDE.md
## records that the copy "omitted the shift, clutch and look-back keys for a
## milestone, which is a driver pressing E, getting nothing, and concluding the
## gearbox is broken", and the same file records the keyboard list once naming F11
## and F12 for actions bound to F3 and F5. Two hand-maintained copies of a list
## that is only ever checked by reading it is two places for that to happen again.
##
## The duplication of `_build_kart` between the two scenes stays deliberate for
## the reason those files give. This is not that: this is one string with no
## per-scene meaning.

## The pad list, in DualSense names.
##
## Face buttons are named as they are printed on the controller rather than by
## Godot's `JOY_BUTTON_*` spelling, because the driver is looking at the pad and
## not at the enum.
##
## **Nothing asserts that this list matches `project.godot`.** It is hand-kept,
## which is the property that let the keyboard version name F11 and F12 for keys
## bound to F3 and F5. `scenes/debug/input_probe.tscn` shows every action live and
## is the check a human can run; a headless assertion that every action named here
## exists in the `InputMap` and carries a joypad event is issue #169.
##
## Throttle, brake and steer are analog and the clutch is not, because both
## triggers are spent — `ARCHITECTURE.md` §6.3's assists exist partly to cover
## that, and #38 is the launch it makes hard.
## Shifts on the face buttons and the clutch on a shoulder, which is the driver's
## own layout rather than the default this shipped with. It puts both shift
## directions under the right thumb where a sequential box wants them, and moves
## the clutch to a finger that is not doing anything else.
##
## The auto-shift toggle went to **R3** when Cross was taken. It is a mode switch
## rather than a control, so it wants the button least likely to be hit by
## accident, and the right stick is not read anywhere in this project. **L1 is
## deliberately unbound** and is the free one if something needs a home.
const PAD_LINE := (
	"R2 throttle  L2 brake  Left stick steer  Square shift up  Cross shift down  "
	+ "R1 clutch  Triangle look back  Circle respawn"
)
const PAD_LINE_2 := (
	"R3 auto-shift on-off  Create camera (chase/cockpit/free)  Options pause"
)

## The keyboard list. The debug keys have no pad binding at all, so they are
## printed whatever is attached.
const KEY_LINE := (
	"W/S throttle-brake  A/D steer  E/Q shift up-down  Shift clutch  "
	+ "C look back  R respawn  V camera (chase/cockpit/free)  G auto-shift"
)
const DEBUG_LINE := "F3 telemetry  F4 freeze frustum  F5 physics draw"

## The menu context. ADR-0053 §2, and the reason it is a separate list rather
## than more entries in the two above: menus and driving are **disjoint input
## contexts**, and printing them as one list would suggest Cross does both things
## at once. It does not — over a live session the pause menu suspends the driver
## through `SessionRunner.set_input_suspended()`, and in the shell there is no
## `PlayerDriver` in the tree at all.
##
## **Every one of these collides with a driving control**, which is not an
## oversight: a DualSense has no spare buttons, and the driving map above already
## spends both triggers, both sticks, all four face buttons, R1, Create, Options
## and the d-pad. `tools/verify/shell_probe.gd` check 8 asserts the overlap
## against a written-down list, so a new collision fails a gate rather than
## surprising a driver.
const MENU_PAD_LINE := (
	"Cross confirm  Circle back  D-pad or left stick to move"
)
const MENU_KEY_LINE := (
	"Enter confirm  Esc back  Arrow keys to move"
)


## The menu bindings, for the pause screen's Controls row and the settings
## screen's binding list. Device-aware for the same reason `lines()` is.
static func menu_lines() -> Array[String]:
	var out: Array[String] = []
	if Input.get_connected_joypads().is_empty():
		out.append(MENU_KEY_LINE)
	else:
		out.append(MENU_PAD_LINE)
		out.append(MENU_KEY_LINE)
	return out


## The lines to print, given what is plugged in.
##
## A pad shows the pad list and the keyboard stays available and unlisted — a
## driver holding a controller does not need to be told about W and S, and the
## line is already long. With no pad, the keyboard list plus a note that nothing
## was detected, so a Bluetooth controller that failed to pair says so instead of
## presenting as an unresponsive game.
static func lines(include_debug: bool = true) -> Array[String]:
	var out: Array[String] = []
	var pads := Input.get_connected_joypads()
	if pads.is_empty():
		out.append(KEY_LINE)
		out.append("no controller detected  -  keyboard only")
	else:
		out.append(PAD_LINE)
		out.append(PAD_LINE_2)
		# Named rather than counted. "1 controller" does not tell somebody whose
		# DualSense is asleep that what answered was the other thing on the desk.
		out.append("%s  -  keyboard also active:  %s"
			% [Input.get_joy_name(pads[0]), KEY_LINE])
	if include_debug:
		out.append(DEBUG_LINE)
	return out
