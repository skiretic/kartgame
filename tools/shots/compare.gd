extends SceneTree

## Compare two stills with a tolerance, and report a number rather than a boolean.
##
##     godot --headless --path . --script tools/shots/compare.gd -- \
##           --a=shots/before.png --b=shots/after.png
##
##     --a, --b            the two images (required)
##     --max_delta=24      largest allowed per-channel difference, 0-255
##     --max_mean=4.0      largest allowed mean difference over the pixels that
##                         changed at all
##     --max_fraction=1.0  largest allowed fraction of changed pixels; off by
##                         default, see below for why
##     --diff=path.png     optional: write a difference visualization
##
## Exits 0 when both limits hold, 1 when either is exceeded, 2 on a usage error.
##
## ## Why a tolerance and not a hash
##
## `cmp` on two renders was the obvious tool and it is the wrong one. Godot stills
## on this host are *usually* byte-identical but not reliably so: repeating one
## unchanged command, about one run in six differs, on roughly half the frame, by
## a mean of 2/255. More settle frames makes it worse rather than better, so it
## accumulates. ADR-0023 has the measurements and issue #102 carries the root
## cause.
##
## The consequence for this project is specific. A hash-based golden-image test
## would fail intermittently for no reason, and a gate that cries wolf gets
## switched off rather than fixed — so `ARCHITECTURE.md` §14's golden-image plan
## needs a threshold to be worth having at all. It also means every look-dev A/B
## should go through this rather than through `cmp`: a real difference of 1/255
## across a surface is a genuine finding, and a boolean cannot tell it apart from
## the noise floor. This prints both numbers so the reader can judge which they
## are looking at.
##
## ## Which statistic actually separates noise from a real change
##
## Not the number of changed pixels, which was the first guess and is useless
## here. Measured on three pairs:
##
## | Pair | Changed | Max delta | Mean over changed |
## |---|---|---|---|
## | Same command twice, byte-identical | 0% | 0 | — |
## | Same command twice, the drift case | 49.7% | 15 | 2.73 |
## | Asphalt ground vs checker ground | 51.9% | 255 | 9.82 |
##
## The drift and the deliberate change touch almost exactly the same *fraction*
## of the frame — about half of it either way — so a fraction threshold cannot
## tell them apart at any setting. What separates them is magnitude: the drift
## tops out around 15-18 and averages under 3, while a real change reaches 255
## and averages near 10. So the defaults threshold on **max delta and mean**, and
## the fraction limit is off unless a caller asks for it.
##
## **The honest limit of this method:** a real change smaller than the noise floor
## is not detectable. A light 1% brighter would produce a max around 3 and a mean
## around 2, which is indistinguishable from a repeat of the same command. That is
## not a flaw in the threshold, it is the noise floor — and it is the concrete
## reason issue #102's root cause is worth finding rather than living with.

func _initialize() -> void:
	var args := Cmdline.parse()

	var path_a := Cmdline.as_string(args, "a", "")
	var path_b := Cmdline.as_string(args, "b", "")
	if path_a.is_empty() or path_b.is_empty():
		push_error("compare.gd: --a and --b are both required")
		quit(2)
		return

	var image_a := Image.load_from_file(path_a)
	var image_b := Image.load_from_file(path_b)
	if image_a == null or image_b == null:
		push_error("compare.gd: could not load %s or %s" % [path_a, path_b])
		quit(2)
		return

	if image_a.get_size() != image_b.get_size():
		# Not a tolerance question. Two different resolutions are not two renders
		# of the same thing, and averaging over a resize would hide that.
		print("DIFFERENT SIZE  %s is %dx%d, %s is %dx%d" % [
			path_a.get_file(), image_a.get_width(), image_a.get_height(),
			path_b.get_file(), image_b.get_width(), image_b.get_height(),
		])
		quit(1)
		return

	var max_delta := Cmdline.as_int(args, "max_delta", 24)
	var max_mean := Cmdline.as_float(args, "max_mean", 4.0)
	var max_fraction := Cmdline.as_float(args, "max_fraction", 1.0)

	image_a.convert(Image.FORMAT_RGB8)
	image_b.convert(Image.FORMAT_RGB8)

	var width := image_a.get_width()
	var height := image_a.get_height()
	var total := width * height

	var diff_image: Image = null
	var diff_path := Cmdline.as_string(args, "diff", "")
	if not diff_path.is_empty():
		diff_image = Image.create(width, height, false, Image.FORMAT_RGB8)

	var changed := 0
	var worst := 0
	var sum_delta := 0.0

	for y in height:
		for x in width:
			var pixel_a := image_a.get_pixel(x, y)
			var pixel_b := image_b.get_pixel(x, y)
			# Compared in 8-bit steps because that is the unit the file stores and
			# the unit a reader can reason about. A float difference of 0.0039 is
			# the same statement as 1/255 and harder to read.
			var dr := absi(int(round(pixel_a.r * 255.0)) - int(round(pixel_b.r * 255.0)))
			var dg := absi(int(round(pixel_a.g * 255.0)) - int(round(pixel_b.g * 255.0)))
			var db := absi(int(round(pixel_a.b * 255.0)) - int(round(pixel_b.b * 255.0)))
			var delta := maxi(dr, maxi(dg, db))
			if delta > 0:
				changed += 1
				sum_delta += float(delta)
				worst = maxi(worst, delta)
			if diff_image != null:
				# Amplified 8x. An honest difference image of a 2/255 drift is
				# indistinguishable from black and tells the reader nothing.
				var scaled := minf(float(delta) * 8.0 / 255.0, 1.0)
				diff_image.set_pixel(x, y, Color(scaled, scaled * 0.35, 0.0))

	var fraction := float(changed) / float(total)
	var mean_over_changed := 0.0 if changed == 0 else sum_delta / float(changed)

	if diff_image != null:
		DirAccess.make_dir_recursive_absolute(diff_path.get_base_dir())
		diff_image.save_png(diff_path)

	var verdict := ""
	var failed := false
	if changed == 0:
		verdict = "IDENTICAL"
	elif worst > max_delta or mean_over_changed > max_mean or fraction > max_fraction:
		verdict = "DIFFERENT"
		failed = true
	else:
		verdict = "WITHIN TOLERANCE"

	print("%s  %d/%d px differ (%.2f%%)  max delta %d  mean over changed %.2f  [limits: delta %d, mean %.1f, fraction %.2f]" % [
		verdict, changed, total, fraction * 100.0, worst, mean_over_changed,
		max_delta, max_mean, max_fraction,
	])

	quit(1 if failed else 0)
