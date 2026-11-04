extends "res://compiler/probe.gd"

const LIBRARY := "res://addons/godot_native/bin/godot_native.windows.template_release.x86_64.dll"


func test_unchanged_export_skips_scons() -> void:
	export_project()
	assert_false(export_project().contains("file(s) for"))


func test_failed_export_removes_library() -> void:
	export_project()
	export_with("#pragma native\n@rpc func f() -> void:\n\tpass\n")
	assert_false(FileAccess.file_exists(LIBRARY))
