#pragma native
extends RefCounted

var kept: PackedInt64Array


func alias_write() -> PackedInt64Array:
	var a := PackedInt64Array()
	a.resize(2)
	var b := a
	b[0] = 9
	return a


func alias_append() -> PackedInt64Array:
	var a := PackedInt64Array()
	var b := a
	b.append(3)
	return a


func write_param(list: PackedFloat64Array) -> void:
	list[0] = 5.0


func append_param(list: PackedInt64Array) -> void:
	list.append(7)


func keep(list: PackedInt64Array) -> void:
	kept = list


func write_kept() -> void:
	kept[0] = 8


func duplicate_stays_apart() -> PackedInt64Array:
	var a := PackedInt64Array()
	a.resize(1)
	var b := a.duplicate()
	b[0] = 2
	return a


func write_int32(list: PackedInt32Array) -> void:
	list[0] = 4
