extends GutTest


func square():
	return load("res://fixtures/square.gd").new()


func test_override() -> void:
	assert_eq(square().area(), 4.0)


func test_super_uses_child_override() -> void:
	assert_eq(square().describe(), "square area 4.0")


func test_is() -> void:
	var shape: Script = load("res://fixtures/shape.gd")
	assert_true(is_instance_of(square(), shape))


func test_unsafe() -> void:
	var values: Array[int] = [1, 2, 3]
	assert_eq(load("res://fixtures/fast.gd").new().sum_array(values), 6)


func test_signals_are_read_from_other_objects() -> void:
	var source = load("res://fixtures/signals.gd").new()
	var child = load("res://fixtures/signals_child.gd").new()
	assert_eq(source.listen(child), [3])
	var seen := []
	child.fired.connect(func(value: int) -> void: seen.append(value))
	child.fire(5)
	assert_eq(seen, [5])


func test_dynamic_call_on_a_script_object() -> void:
	if Engine.get_version_info().minor < 5:
		pending("scripts answer static calls from Godot 4.5")
		return
	var script = load("res://fixtures/signals.gd")
	assert_eq(script.new().through_script(script), 8)


func test_is_checks_ancestors_and_script_objects() -> void:
	var script = load("res://fixtures/signals.gd")
	var source = script.new()
	assert_true(source.matches(load("res://fixtures/signals_child.gd").new()))
	assert_true(source.is_instance(load("res://fixtures/signals_child.gd").new()))
	assert_false(source.matches(script))
	assert_false(source.matches(RefCounted.new()))


func test_line_continuation() -> void:
	assert_eq(load("res://fixtures/signals.gd").new().continued(1), 2)


func test_inherited_engine_constant() -> void:
	assert_eq(load("res://fixtures/signals.gd").new().notification_id(), Object.NOTIFICATION_PREDELETE)


func test_vector_from_one_vector() -> void:
	assert_eq(load("res://fixtures/signals.gd").new().widened(Vector2i(3, 4)), Vector2(3, 4))


func test_float_with_leading_dot() -> void:
	assert_eq(load("res://fixtures/signals.gd").new().leading_dot(), 0.5)


func test_engine_enum_through_its_name() -> void:
	assert_eq(load("res://fixtures/signals.gd").new().enum_value(), GeometryInstance3D.SHADOW_CASTING_SETTING_DOUBLE_SIDED)


func test_setter_that_narrows_to_bool() -> void:
	var source: RefCounted = load("res://fixtures/signals.gd").new()
	source.cost = 5.0
	assert_eq(source.read_cost(), 1.0)


func test_constant_from_a_utility_call() -> void:
	assert_almost_eq(load("res://fixtures/signals.gd").new().half_turn(), PI, 0.0001)


func test_builtin_values_from_numbers() -> void:
	assert_eq(load("res://fixtures/signals.gd").new().direct_values(1.0), [Color(1.0, 0.5, 0.25), Color(1.0, 0.5, 0.25, 0.75), Vector2i(3, 4), Vector3i(1, 2, 3), Vector4(1.0, 2.0, 3.0, 4.0), Vector4i(1, 2, 3, 4), Rect2(1.0, 2.0, 3.0, 4.0), Rect2i(1, 2, 3, 4), Plane(0.0, 1.0, 0.0, 1.0), Quaternion(0.0, 0.0, 0.0, 1.0)])


func test_method_on_freed_object_stops_the_call() -> void:
	var source: RefCounted = load("res://fixtures/signals.gd").new()
	var node := Node.new()
	node.add_child(Node.new())
	source.hold(node)
	assert_eq(source.held_count(), 1)
	node.free()
	if source.get_script() is GDScript:
		return
	assert_ne(source.held_count(), 1)
	assert_false(source.matches(node))
	if has_method("assert_engine_error_count"):
		call("assert_engine_error_count", 1)
