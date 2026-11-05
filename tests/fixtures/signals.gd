#pragma native
class_name NativeSignals extends RefCounted

signal fired(value: int)


static func twice(value: int) -> int:
	return value * 2


func fire(value: int) -> void:
	fired.emit(value)


func listen(other: Variant) -> Array:
	var seen := []
	other.fired.connect(func(value: int) -> void: seen.append(value))
	other.fire(3)
	return seen


func through_script(script: Variant) -> int:
	return script.twice(4)


func matches(other: Variant) -> bool:
	return other is NativeSignals


func is_instance(other: Variant) -> bool:
	return is_instance_of(other, NativeSignals)


func continued(value: int) -> int:
	return value + \
		1


func notification_id() -> int:
	return NOTIFICATION_PREDELETE


func widened(v: Vector2i) -> Vector2:
	return Vector2(v)


func leading_dot() -> float:
	return .5


func enum_value() -> int:
	return GeometryInstance3D.ShadowCastingSetting.SHADOW_CASTING_SETTING_DOUBLE_SIDED


const HALF_TURN: float = deg_to_rad(180)
var cost: float = 0.0 : set = set_cost


func set_cost(value: bool) -> void:
	cost = value


func read_cost() -> float:
	return cost


func half_turn() -> float:
	return HALF_TURN


func direct_values(x: float) -> Array:
	return [Color(x, 0.5, 0.25), Color(x, 0.5, 0.25, 0.75), Vector2i(3, 4), Vector3i(1, 2, 3), Vector4(x, 2.0, 3.0, 4.0), Vector4i(1, 2, 3, 4), Rect2(x, 2.0, 3.0, 4.0), Rect2i(1, 2, 3, 4), Plane(0.0, 1.0, 0.0, x), Quaternion(0.0, 0.0, 0.0, x)]


var held: Node


func hold(node: Node) -> void:
	held = node


func held_count() -> int:
	return held.get_child_count()
