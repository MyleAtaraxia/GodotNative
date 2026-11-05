extends GutTest


var base: Node
var child: Node


func before_each() -> void:
	base = load("res://fixtures/direct_base.gd").new()
	child = load("res://fixtures/direct_child.gd").new()


func after_each() -> void:
	base.free()
	child.free()


func test_calls_reach_the_receiver() -> void:
	assert_eq(base.sum_of(base), 1 + 1 + 4 + 12)


func test_calls_reach_an_override() -> void:
	assert_eq(base.sum_of(child), 101 + 1 + 4 + 12)
	assert_eq(base.walk(base.list_of(base, child)), 102)


func test_member_writes_reach_the_receiver() -> void:
	base.write(child)
	assert_eq(child.value, 8)
	assert_eq(child.spot, Vector3(2.0, 0.0, 0.0))
	assert_eq(child.label, "base!")
	assert_eq(child.doubled, 8)
	assert_eq(base.value, 1)


func test_chained_members_and_returns() -> void:
	assert_eq(base.chained(child), 9)
	child.peer = null


func test_await_on_a_receiver() -> void:
	assert_eq(await base.awaited(child), 106)


func test_freed_receiver_stops_the_call() -> void:
	var other := load("res://fixtures/direct_base.gd").new() as Node
	base.peer = other
	assert_eq(base.held_number(), 1)
	other.free()
	if base.get_script() is GDScript:
		return
	assert_ne(base.held_number(), 1)
	assert_ne(base.held_value(), 1)
	if has_method("assert_engine_error_count"):
		call("assert_engine_error_count", 2)
