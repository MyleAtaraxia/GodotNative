#pragma native
class_name DirectBase
extends Node


var value: int = 1
var label: String = "base"
var spot: Vector3
var peer: DirectBase
var doubled: int:
	set(v):
		doubled = v * 2


func number() -> int:
	return 1


func add(a: int, b: int = 10) -> int:
	return value + a + b


func make_peer() -> DirectBase:
	return self


func sum_of(other: DirectBase) -> int:
	return other.number() + other.value + other.add(1, 2) + other.add(1)


func write(other: DirectBase) -> void:
	other.value = 7
	other.value += 1
	other.spot.x = 2.0
	other.label += "!"
	other.doubled = 4


func chained(other: DirectBase) -> int:
	other.peer = other
	other.peer.peer.value = 9
	return other.peer.peer.make_peer().value


func list_of(a: DirectBase, b: DirectBase) -> Array[DirectBase]:
	var list: Array[DirectBase] = [a, b]
	return list


func walk(items: Array[DirectBase]) -> int:
	var total := 0
	for item in items:
		total += item.number()
	return total


func deferred() -> int:
	await Engine.get_main_loop().process_frame
	return 5


func awaited(other: DirectBase) -> int:
	var first := await other.deferred()
	return first + other.number()


func held_number() -> int:
	return peer.number()


func held_value() -> int:
	return peer.value


