extends GutTest


func test_property_setter_and_signal() -> void:
	var thing = autofree(load("res://fixtures/thing.gd").new())
	watch_signals(thing)
	thing.limited = 99
	assert_eq(thing.limited, 10)
	assert_signal_emitted_with_parameters(thing, "changed", [10])


func test_inner_class() -> void:
	var thing = autofree(load("res://fixtures/thing.gd").new())
	assert_eq(thing.boxed(), 14)


func test_instance_reaches_static_member() -> void:
	var thing = autofree(load("res://fixtures/thing.gd").new())
	assert_gt(thing.created, 0)


func test_static_call_on_script() -> void:
	var script: Script = load("res://fixtures/thing.gd")
	if Engine.get_version_info().minor < 5 and script.get_class() == "NativeScript":
		pending("Script-level statics need Godot 4.5")
		return
	assert_eq(script.label(), "thing")
