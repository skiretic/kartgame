class_name Cmdline
extends RefCounted

## Parses the `--key=value` arguments that follow a bare `--` on Godot's command
## line.
##
## Godot consumes everything before the `--` itself, so user arguments are the
## only place a scene can be parameterized from a shell script. Every look-dev
## still in this project is produced by one invocation with different values
## here, which is what makes a render reproducible from the command that made it
## rather than from an editor session nobody else can replay.


## `--speed=80 --debug` becomes `{"speed": "80", "debug": "true"}`.
##
## A flag with no `=` is `"true"` rather than `""`, so `as_bool` treats presence
## and `--flag=true` identically.
static func parse() -> Dictionary:
	var parsed := {}
	for argument in OS.get_cmdline_user_args():
		if not argument.begins_with("--"):
			continue
		var pair := argument.substr(2).split("=", true, 1)
		parsed[pair[0]] = pair[1] if pair.size() > 1 else "true"
	return parsed


static func as_float(args: Dictionary, key: String, fallback: float) -> float:
	return float(args[key]) if args.has(key) else fallback


static func as_int(args: Dictionary, key: String, fallback: int) -> int:
	return int(args[key]) if args.has(key) else fallback


static func as_bool(args: Dictionary, key: String, fallback: bool) -> bool:
	if not args.has(key):
		return fallback
	var value := String(args[key]).to_lower()
	return value in ["true", "1", "yes", "on"]


static func as_string(args: Dictionary, key: String, fallback: String) -> String:
	return String(args[key]) if args.has(key) else fallback
