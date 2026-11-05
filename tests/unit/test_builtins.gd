extends GutTest


var source: RefCounted


func before_each() -> void:
	source = load("res://fixtures/builtins.gd").new()


func expected_transform() -> Transform3D:
	return Transform3D(Basis(Vector3.UP, 0.5), Vector3(1.0, 2.0, 3.0))


func test_transform_arithmetic() -> void:
	var t := expected_transform()
	var p := Vector3(1.0, 0.0, 2.0)
	assert_eq(source.transforms(t, p), (t * t).origin + t * p + t.basis * p + t.basis.x)


func test_quaternion_arithmetic() -> void:
	var q := Quaternion(Vector3.UP, 0.5)
	var p := Vector3(1.0, 0.0, 2.0)
	assert_eq(source.rotations(q, p), (q * q) * p + q.slerp(Quaternion.IDENTITY, 0.5) * p)


func test_writing_nested_fields() -> void:
	var t := Transform3D()
	t.origin = Vector3(3.0, 1.0, 0.0)
	t.basis.y = Vector3(1.0, 2.0, 3.0)
	t.basis.x.z = 7.0
	assert_eq(source.written_transform(), t)


func test_writing_nested_fields_of_a_member() -> void:
	var t := Transform3D()
	t.origin.z = 4.0
	t.basis.z.x = 5.0
	assert_eq(source.written_member(), t)
	assert_eq(source.stored_transform, t)


func test_color_math() -> void:
	var c := Color(0.5, 0.25, 1.0, 1.0)
	var d := c * 0.5 + Color(0.25, 0.25, 0.25, 0.0)
	d.a = 0.5
	assert_eq(source.color_math(c), d.lerp(Color.WHITE, 0.5))
	assert_eq(source.stored_color, d)


func test_default_values() -> void:
	assert_eq(source.default_values(), [Transform3D(), Basis(), Quaternion(), Color(), Rect2(), AABB(), Vector2i(), Color()])


func test_integer_vectors() -> void:
	assert_eq(source.integers(Vector2i(1, 2), Vector3i(1, 2, 3)), [Vector2i(7, 4), Vector3i(-1, -2, 9), Vector2i(2, 4), Vector2(1.0, 2.0), Vector3i(1, 2, 3)])


func test_rectangles() -> void:
	var box := AABB(Vector3.ONE, Vector3.ONE)
	var grown := box
	grown.size *= 2.0
	var r := Rect2(0.0, 1.0, 2.0, 2.0)
	assert_eq(source.rectangles(r, box), [Rect2(1.0, 1.0, 2.0, 2.0), true, grown, grown.get_center(), r.end])


func test_transform_2d() -> void:
	var t := Transform2D(0.5, Vector2(1.0, 2.0))
	var moved := t
	moved.origin.y = 8.0
	assert_eq(source.flat(t, Vector2.ONE), [moved, t * Vector2.ONE, (t * t).x, t.affine_inverse().origin])


func test_conversions() -> void:
	var q := Quaternion(Vector3.UP, 0.5)
	var b := Basis(q)
	assert_eq(source.conversions(q, Vector2i(5, 6)), [b, Vector2(5.0, 6.0), Vector2(1.0, 2.0), Transform3D(b, Vector3(1.0, 2.0, 3.0)), q, AABB(Vector3.ONE, Vector3.ONE)])


func test_comparisons() -> void:
	var t := expected_transform()
	assert_eq(source.compare(t, t, Color.WHITE), [true, false, true, true])
	assert_eq(source.compare(t, Transform3D(), Color.RED), [false, true, false, true])


func test_math_methods() -> void:
	var v := Vector3(-1.0, 2.0, 3.0)
	var c := Color(0.5, 0.25, 1.0)
	assert_eq(source.methods(v, c), [v.lerp(Vector3.ONE, 0.5), v.abs(), v.rotated(Vector3.UP, 1.0), c.to_html(), Vector2(1.0, 2.0).angle(), Basis.IDENTITY.rotated(Vector3.UP, 0.5).get_euler()])


func test_container_methods() -> void:
	var list := [3, 1]
	var map := { "a": 1 }
	assert_eq(source.containers(list, map, "ab,c"), [[1, 3, 9, 10], true, [3, 9], 2, true, 1, null, 5, ["a"], "AB,C", PackedStringArray(["ab", "c"]), true, "b,c"])
	assert_eq(list, [3, 1])


func test_transforms_in_a_typed_array() -> void:
	var items: Array[Transform3D] = [Transform3D(Basis(), Vector3.ONE), Transform3D(Basis(), Vector3.ONE)]
	assert_eq(source.stored_in_array(items), Vector3(102.0, 3.0, 3.0))
	assert_eq(items[0].origin.x, 100.0)


func test_writing_through_a_property() -> void:
	var node := Node3D.new()
	var expected := Transform3D()
	expected.origin = Vector3(2.0, 3.0, 0.0)
	expected.basis.x = Vector3(2.0, 0.0, 0.0)
	assert_eq(source.through_property(node), expected * Vector3.ONE)
	assert_eq(node.transform, expected)
	node.free()


func test_type_checks() -> void:
	assert_eq(source.iterate(Color.RED, Transform3D()), 2)


func test_for_over_typed_arrays() -> void:
	var numbers: Array[int] = [1, 2, 3]
	assert_eq(source.sum_typed(numbers), 12)
	assert_eq(source.sum_annotated([1.0, 3.0]), 2.0)
	assert_eq(source.sum_annotated([1, 3]), 2.0)
	var vectors: Array[Vector3] = [Vector3.ONE]
	assert_eq(source.transforms_while_growing(vectors), Vector3.ONE * 15.0)
	assert_eq(vectors.size(), 4)
