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
