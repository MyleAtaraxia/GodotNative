extends GutTest

var sharing: Object


func before_each() -> void:
	sharing = load("res://fixtures/packed_sharing.gd").new()


func test_a_second_variable_writes_to_the_same_array() -> void:
	assert_eq(sharing.alias_write(), PackedInt64Array([9, 0]))
	assert_eq(sharing.alias_append(), PackedInt64Array([3]))


func test_writes_to_a_parameter_reach_the_caller() -> void:
	var floats := PackedFloat64Array([1.0])
	sharing.write_param(floats)
	assert_eq(floats, PackedFloat64Array([5.0]))
	var ints := PackedInt64Array()
	sharing.append_param(ints)
	assert_eq(ints, PackedInt64Array([7]))


func test_a_member_shares_with_the_caller() -> void:
	var list := PackedInt64Array([1])
	sharing.keep(list)
	sharing.write_kept()
	assert_eq(list, PackedInt64Array([8]))
	list[0] = 3
	assert_eq(sharing.kept, PackedInt64Array([3]))


func test_duplicate_makes_a_separate_array() -> void:
	assert_eq(sharing.duplicate_stays_apart(), PackedInt64Array([0]))


func test_other_packed_types_share_too() -> void:
	var list := PackedInt32Array([1])
	sharing.write_int32(list)
	assert_eq(list, PackedInt32Array([4]))
