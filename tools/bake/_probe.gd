extends SceneTree

func _initialize() -> void:
	print("--- editor hint: ", Engine.is_editor_hint())
	for cls in ["LightmapGI", "LightmapGIData", "Lightmapper", "LightmapperRD"]:
		print("class %s exists: %s" % [cls, ClassDB.class_exists(cls)])
		if ClassDB.class_exists(cls):
			var names := []
			for m in ClassDB.class_get_method_list(cls, true):
				names.append(m["name"])
			print("   own methods: ", names)
	print("--- PrimitiveMesh.add_uv2 test")
	var box := BoxMesh.new()
	box.size = Vector3(2, 2, 2)
	print("   default format has UV2: ", (box.surface_get_format(0) & Mesh.ARRAY_FORMAT_TEX_UV2) != 0)
	box.add_uv2 = true
	print("   add_uv2=true has UV2:   ", (box.surface_get_format(0) & Mesh.ARRAY_FORMAT_TEX_UV2) != 0)
	var arrays := box.surface_get_arrays(0)
	var uv2: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV2]
	print("   uv2 count %d, first few %s" % [uv2.size(), uv2.slice(0, 4)])
	print("--- ArrayMesh.lightmap_unwrap test")
	var am := ArrayMesh.new()
	var plain := BoxMesh.new()
	am.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, plain.surface_get_arrays(0))
	var err := am.lightmap_unwrap(Transform3D.IDENTITY, 0.1)
	print("   lightmap_unwrap -> ", err, " (OK=", OK, ")")
	print("   result has UV2: ", (am.surface_get_format(0) & Mesh.ARRAY_FORMAT_TEX_UV2) != 0)
	quit(0)
