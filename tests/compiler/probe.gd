extends GutTest

const PROBE := "res://probe/probe.gd"
const ADDON_PROBE := "res://addons/probe.gd"

var project_text := ""
var written: Array[String] = []


func before_each() -> void:
	project_text = FileAccess.get_file_as_string("res://project.godot")
	DirAccess.make_dir_recursive_absolute("res://probe")


func after_each() -> void:
	FileAccess.open("res://project.godot", FileAccess.WRITE).store_string(project_text)
	for path: String in written:
		DirAccess.remove_absolute(path)
		DirAccess.remove_absolute(path + ".uid")
	written.clear()


func write_file(path: String, text: String) -> void:
	FileAccess.open(path, FileAccess.WRITE).store_string(text)
	written.append(path)


func export_project() -> String:
	DirAccess.make_dir_recursive_absolute("res://build")
	var args := ["--headless", "--path", ProjectSettings.globalize_path("res://"), "--export-pack", "test", "build/probe.pck"]
	var output := []
	OS.execute(OS.get_executable_path(), args, output, true)
	return "".join(output)


func export_with(source: String, setting := "", path := PROBE) -> String:
	write_file(path, source)
	if not setting.is_empty():
		FileAccess.open("res://project.godot", FileAccess.WRITE).store_string(project_text + "\n[godot_native]\n\n" + setting + "\n")
	return export_project()
