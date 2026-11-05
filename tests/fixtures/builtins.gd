#pragma native
extends RefCounted


var stored_transform: Transform3D
var stored_color: Color


func transforms(t: Transform3D, point: Vector3) -> Vector3:
	return (t * t).origin + t * point + t.basis * point + t.basis.x


func rotations(q: Quaternion, point: Vector3) -> Vector3:
	return (q * q) * point + q.slerp(Quaternion.IDENTITY, 0.5) * point


func written_transform() -> Transform3D:
	var t := Transform3D()
	t.origin.x = 3.0
	t.origin += Vector3(0.0, 1.0, 0.0)
	t.basis.y = Vector3(1.0, 2.0, 3.0)
	t.basis.x.z = 7.0
	return t


func written_member() -> Transform3D:
	stored_transform.origin.z = 4.0
	stored_transform.basis.z.x = 5.0
	return stored_transform


func color_math(c: Color) -> Color:
	var d := c * 0.5 + Color(0.25, 0.25, 0.25, 0.0)
	d.a = 0.5
	stored_color = d
	return d.lerp(Color.WHITE, 0.5)


func default_values() -> Array:
	var t: Transform3D
	var b: Basis
	var q: Quaternion
	var c: Color
	var r: Rect2
	var a: AABB
	var i: Vector2i
	return [t, b, q, c, r, a, i, stored_color]


func integers(a: Vector2i, b: Vector3i) -> Array:
	var c := a + Vector2i(1, 2)
	c.x += 5
	var d := -b
	d.z = 9
	return [c, d, a * 2, Vector2(a), Vector3i(Vector3(1.9, 2.1, 3.5))]


func rectangles(r: Rect2, box: AABB) -> Array:
	var moved := r
	moved.position.x += 1.0
	var grown := box
	grown.size *= 2.0
	return [moved, moved.has_point(Vector2(1.5, 1.5)), grown, grown.get_center(), r.end]


func flat(t: Transform2D, point: Vector2) -> Array:
	var moved := t
	moved.origin.y = 8.0
	return [moved, t * point, (t * t).x, t.affine_inverse().origin]


func conversions(q: Quaternion, stored: Variant) -> Array:
	var b: Basis = Basis(q)
	var from_variant: Vector2 = stored
	var v2: Vector2 = Vector2i(1, 2)
	return [b, from_variant, v2, Transform3D(b, Vector3(1.0, 2.0, 3.0)), Quaternion(Vector3.UP, 0.5), AABB(Vector3.ONE, Vector3.ONE)]


func compare(a: Transform3D, b: Transform3D, c: Color) -> Array:
	return [a == b, a != b, c == Color.WHITE, Color(1.0, 1.0, 1.0) == Color.WHITE]


func methods(v: Vector3, c: Color) -> Array:
	return [v.lerp(Vector3.ONE, 0.5), v.abs(), v.rotated(Vector3.UP, 1.0), c.to_html(), Vector2(1.0, 2.0).angle(), Basis.IDENTITY.rotated(Vector3.UP, 0.5).get_euler()]


func containers(list: Array, map: Dictionary, text: String) -> Array:
	var copy := list.duplicate()
	copy.push_back(9)
	copy.append(10)
	copy.sort()
	return [copy, copy.has(9), copy.slice(1, 3), copy.find(9), map.has("a"), map.get("a"), map.get("missing"), map.get("missing", 5), map.keys(), text.to_upper(), text.split(","), text.begins_with("a"), text.substr(1)]


func stored_in_array(items: Array[Transform3D]) -> Vector3:
	var sum := Vector3.ZERO
	for item: Transform3D in items:
		sum += item.origin
	items[0].origin.x = 100.0
	return sum + items[0].origin


func through_property(node: Node3D) -> Vector3:
	node.transform.origin.x = 2.0
	node.position.y = 3.0
	node.transform.basis.x = Vector3(2.0, 0.0, 0.0)
	return node.transform * Vector3.ONE


func iterate(c: Color, t: Transform3D) -> int:
	var total := 0
	if c is Color and t is Transform3D:
		total += 1
	var boxed: Variant = t
	if boxed is Transform3D:
		total += 1
	return total


func sum_typed(items: Array[int]) -> int:
	var total := 0
	for item in items:
		total += item * 2
	return total


func sum_annotated(items: Array) -> float:
	var total := 0.0
	for item: float in items:
		total += item / 2.0
	return total


func transforms_while_growing(items: Array[Vector3]) -> Vector3:
	var total := Vector3.ZERO
	for item in items:
		total += item
		if items.size() < 4:
			items.append(item * 2.0)
	return total
