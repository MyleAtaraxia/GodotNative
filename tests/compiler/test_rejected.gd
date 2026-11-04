extends "res://compiler/probe.gd"

const CASES := [
	["missing return", "code paths", "#pragma native\nfunc f(x: int) -> int: # error\n\tif x > 0:\n\t\treturn 1\n"],
	["missing return in loop", "code paths", "#pragma native\nfunc f() -> int: # error\n\tfor i in range(3):\n\t\treturn i\n"],
	["missing return in match", "code paths", "#pragma native\nfunc f(x: int) -> int: # error\n\tmatch x:\n\t\t1:\n\t\t\treturn 1\n"],
	["bare return", "must return a value", "#pragma native\nfunc f() -> int:\n\treturn # error\n"],
	["void returns value", "cannot return a value", "#pragma native\nfunc f() -> void:\n\treturn 1 # error\n"],
	["static calls instance", "instance function", "#pragma native\nfunc g() -> void:\n\tpass\nstatic func f() -> void:\n\tg() # error\n"],
	["static reads member", "instance member", "#pragma native\nvar a: int = 0\nstatic func f() -> int:\n\treturn a # error\n"],
	["static uses self", "cannot use self", "#pragma native\nstatic func f() -> void:\n\tprint(self) # error\n"],
	["unrelated class", "unrelated type", "#pragma native\nfunc f(n: Node) -> Resource:\n\tvar r: Resource = n # error\n\treturn r\n"],
	["wrong array element", "array literal", "#pragma native\nfunc f() -> void:\n\tvar a: Array[int] = [\"x\"] # error\n\tprint(a)\n"],
	["duplicate member", "declared twice", "#pragma native\nvar a: int = 0\nvar a: int = 1 # error\n"],
	["shadowed member", "already exists", "#pragma native\nextends Calc\nvar total: int = 0 # error\n"],
	["onready outside a node", "@onready", "#pragma native\nextends RefCounted\n@onready var a: int = 1 # error\n"],
	["unsupported annotation", "not supported", "#pragma native\n@rpc func f() -> void: # error\n\tpass\n"],
	["unsupported child of native", "child of native script", "extends Calc\n@rpc func f() -> void: # error\n\tpass\n"],
]


func error_line(source: String) -> int:
	var lines := source.split("\n")
	for i in lines.size():
		if lines[i].ends_with("# error"):
			return i + 1
	return 0


func test_rejected(case: Array = use_parameters(CASES)) -> void:
	var output := export_with(case[2])
	assert_string_contains(output, "probe.gd:%d:" % error_line(case[2]), case[0])
	assert_string_contains(output, case[1], case[0])
