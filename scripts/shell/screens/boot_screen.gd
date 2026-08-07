extends ShellScreen

## Plate 4. The screen-document family: a wordmark, a check line, and a way in.
##
## The check line is the point of the screen. ADR-0052: **a missing generated
## asset replaces it with the exact command to run, in words, and boot stops** —
## no spinner, no silent black, no menu that leads to a session that cannot
## build. This project generates its own kart and its own circuit, so "the assets
## are not there yet" is a normal state on a fresh clone and the honest response
## is a sentence naming `genkart.sh`, not a crash three screens later.
##
## `--backdrop=flat` declares that it needs no generated asset, so it is not
## blocked by one being absent. That is what lets the gate and a fresh worktree
## reach the paddock.

const REQUIRED := {
	"res://assets/generated/kart.glb": "tools/blender/genkart.sh",
	"res://assets/generated/valdirone_nuova.glb": "tools/blender/gentrack.sh",
}

var _blocked := PackedStringArray()
var _enter: Button


func can_pop() -> bool:
	return false


func build() -> void:
	add_child(ShellTheme.ground(false))

	var rows := VBoxContainer.new()
	rows.set_anchors_preset(Control.PRESET_FULL_RECT)
	rows.add_theme_constant_override("separation", 0)
	add_child(rows)

	rows.add_child(_wordmark())
	rows.add_child(ShellTheme.rule(false, false))
	rows.add_child(_bar())


func _wordmark() -> Control:
	var box := CenterContainer.new()
	box.size_flags_vertical = Control.SIZE_EXPAND_FILL

	var column := VBoxContainer.new()
	column.alignment = BoxContainer.ALIGNMENT_CENTER
	column.add_theme_constant_override("separation", 14)

	var mark := ShellTheme.label("KARTGAME", ShellTheme.T_WORDMARK, ShellTheme.SCR_INK,
			ShellTheme.Weight.BOLD, ShellTheme.TRACK_WORDMARK_EM)
	mark.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	column.add_child(mark)

	# The plate's own placeholder text, kept verbatim. Naming is cheap and late,
	# and a made-up series name on the boot screen would be the one piece of
	# fiction in the shell that nobody decided.
	var sub := ShellTheme.kicker("series name — open, naming is cheap and late",
			ShellTheme.SCR_SOFT)
	sub.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	column.add_child(sub)

	box.add_child(column)
	return box


func _bar() -> Control:
	var bar := PanelContainer.new()
	var box := StyleBoxFlat.new()
	box.bg_color = ShellTheme.SCR_CHROME
	box.content_margin_left = 20.0
	box.content_margin_right = 20.0
	box.content_margin_top = 10.0
	box.content_margin_bottom = 10.0
	bar.add_theme_stylebox_override("panel", box)

	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 24)
	bar.add_child(row)

	var checks := _check_line()
	checks.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	row.add_child(checks)

	# Focusable even though the screen also takes any key: `shell_probe.gd`
	# check 4 wants focus to land on something with a rect inside the viewport,
	# and a screen whose only affordance is "press any key" would otherwise have
	# nowhere to put it. It is also the honest pad affordance — Cross works here
	# for the same reason it works everywhere else.
	_enter = ShellTheme.row_button("Press any key", ShellTheme.SCR_INK, ShellTheme.T_FOOT)
	_enter.disabled = not _blocked.is_empty()
	_enter.pressed.connect(_advance)
	row.add_child(_enter)
	return bar


## `extension ✓ · kart.glb ✓ · audio device ✓`, or the command that fixes it.
func _check_line() -> Control:
	var column := VBoxContainer.new()
	column.add_theme_constant_override("separation", 4)

	var parts := PackedStringArray()
	parts.append("extension %s" % _tick(ClassDB.class_exists("KartBody")))

	# Typed explicitly: `shell` is a plain `Node` here rather than a `ShellRoot`,
	# because typing it would close a `ShellScreen -> ShellRoot -> ScreenStack ->
	# ShellScreen` cycle, and GDScript resolves those badly. So every call through
	# it returns Variant and the inference has to be given a hand.
	var backdrop: ShellBackdrop = shell.backdrop() if shell != null else null
	var flat: bool = backdrop != null and backdrop.mode() == "flat"
	for path: String in REQUIRED:
		var present := ResourceLoader.exists(path)
		parts.append("%s %s" % [path.get_file(), _tick(present)])
		if not present and not flat:
			_blocked.append("%s is not built — run %s" % [path.get_file(), REQUIRED[path]])

	parts.append("audio %s" % _tick(not AudioServer.get_output_device().is_empty()))
	column.add_child(ShellTheme.kicker(" · ".join(parts), ShellTheme.SCR_SOFT))

	if _blocked.is_empty():
		var notes: PackedStringArray = shell.boot_notes() if shell != null \
				else PackedStringArray()
		for note: String in notes:
			column.add_child(ShellTheme.label(note, ShellTheme.T_COLHEAD,
					ShellTheme.SCR_SOFT))
		return column

	for line: String in _blocked:
		column.add_child(ShellTheme.label(line, ShellTheme.T_FOOT, ShellTheme.ST_SLOWER,
				ShellTheme.Weight.SEMIBOLD))
	return column


static func _tick(ok: bool) -> String:
	return "ok" if ok else "MISSING"


## Any key, as the plate says — not just `menu_confirm`. This is the one screen
## where that is the whole interaction, so it reads raw input rather than an
## action, and it runs in `_unhandled_input` so the stack's own `_input` still
## gets first refusal on the menu actions.
func _unhandled_input(event: InputEvent) -> void:
	if not _blocked.is_empty():
		return
	var pressed := (event is InputEventKey and event.is_pressed() and not event.is_echo()) \
			or (event is InputEventJoypadButton and event.is_pressed()) \
			or (event is InputEventMouseButton and event.is_pressed())
	if pressed:
		get_viewport().set_input_as_handled()
		_advance()


## `reset_to`, not `push`: boot is not somewhere you go back to, and leaving it
## under the paddock would make Circle at the paddock reveal a boot screen.
func _advance() -> void:
	if shell != null:
		shell.reset_to("paddock")


func initial_focus() -> Control:
	return _enter


func title() -> String:
	return "boot"
