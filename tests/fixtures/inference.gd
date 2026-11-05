#pragma native
extends Node
@warning_ignore_start("untyped_declaration")

class Plain:
	func area():
		return 1

class Fancy extends Plain:
	func area():
		return "big"

var counter = 0
var ratio = 0.5
var changes = 1
var from_outside = 1
var by_name = 2
var words = []
var scaled = doubled(4)


func answer():
	return 42


func half():
	return answer() / 2.0


func doubled(value: int):
	return value * 2


func mixed(flag: bool):
	if flag:
		return 1
	return "text"


func maybe(flag: bool):
	if flag:
		return 1


func bare(flag: bool):
	if flag:
		return
	return 1


func int_or_float(flag: bool):
	if flag:
		return 1
	return 1.5


func with_lambda():
	var inner = func(): return "inner"
	return inner.call().length()


func countdown(left: int):
	if left <= 0:
		return 0
	return 1 + countdown(left - 1)


func areas():
	return [Plain.new().area(), Fancy.new().area()]


func keeps_class():
	var node = Node.new()
	node.add_child(Node.new())
	var count = node.get_child_count()
	node.free()
	return count


func changes_class():
	var value = Node.new()
	var count = value.get_child_count()
	value.free()
	value = RefCounted.new()
	return [count, value.get_class()]


func becomes_null():
	var node = Node.new()
	node.free()
	node = null
	return node == null


func child_totals():
	var total = 0
	for child in get_children():
		total += child.get_child_count()
	return total


func narrowed(value: Variant):
	if value is Node:
		return value.get_child_count()
	return -1


func narrowed_script(value: Variant):
	if value is Plain:
		return value.area()
	return "none"


func narrowed_then_written(value: Variant):
	if value is Node:
		value = 5
		return value
	return 0


func not_narrowed(value: Variant):
	if value is not Node:
		return "other"
	return value.get_child_count()


func bump():
	counter += 1
	ratio *= 2.0
	return [counter, ratio]


func rewrite():
	changes = "text"
	return changes


func integer_division():
	var count = 7
	return count / 2


func add_word(word: String):
	words.append(word)
	return words.size()
