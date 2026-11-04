#pragma native
class_name Thing
extends Node

signal changed(value: int)

static var created: int = 0

var limited: int = 0:
	set(value):
		limited = clampi(value, 0, 10)
		changed.emit(limited)


class Box:
	var value: int = 7

	func twice() -> int:
		return value * 2


func _init() -> void:
	created += 1


func boxed() -> int:
	return Box.new().twice()


static func label() -> String:
	return "thing"


func parent_state() -> String:
	var dynamic: Variant = self
	var parent: Node = dynamic.call("get_parent")
	return "orphan" if parent == null else "attached"
