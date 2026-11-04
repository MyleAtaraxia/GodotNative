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
	["parallel break", "cannot use break", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel for\n\tfor i in 3:\n\t\tbreak # error\n"],
	["parallel return", "cannot use return", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel for\n\tfor i in 3:\n\t\treturn # error\n"],
	["parallel await", "uses await", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel for\n\tfor i in 3: # error\n\t\tawait get_tree().process_frame\n"],
	["parallel over an array", "needs range", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel for\n\tfor i in [1, 2]: # error\n\t\tprint(i)\n"],
	["pragma without a loop", "right before a for loop", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel for # error\n\tprint(1)\n"],
	["unsupported directive", "unsupported OpenMP directive", "#pragma native\nfunc f() -> void:\n\t#pragma omp task # error\n\tprint(1)\n"],
	["missing end", "missing #pragma end parallel", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel # error\n\tprint(1)\n"],
	["stray end", "no matching start", "#pragma native\nfunc f() -> void:\n\tprint(1)\n\t#pragma end critical # error\n"],
	["wrong end", "no matching start", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel\n\t#pragma end single # error\n"],
	["return in a block", "cannot use return", "#pragma native\nfunc f() -> void:\n\t#pragma omp critical\n\treturn # error\n\t#pragma omp end critical\n"],
	["continue out of a block", "cannot use continue", "#pragma native\nfunc f() -> void:\n\tfor i in 3:\n\t\t#pragma omp critical\n\t\tcontinue # error\n\t\t#pragma omp end critical\n"],
	["barrier in a loop", "cannot be nested", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel for\n\tfor i in 3:\n\t\t#pragma omp barrier # error\n"],
	["for in a single", "cannot be nested", "#pragma native\nfunc f() -> void:\n\t#pragma omp single\n\t#pragma omp for\n\tfor i in 3: # error\n\t\tprint(i)\n\t#pragma end single\n"],
	["atomic assignment", "like 'x += y'", "#pragma native\nfunc f() -> void:\n\tvar x: int = 0\n\t#pragma omp atomic\n\tx = 1 # error\n\tprint(x)\n"],
	["atomic on an array", "needs an int or float", "#pragma native\nfunc f() -> void:\n\tvar x := \"a\"\n\t#pragma omp atomic\n\tx += \"b\" # error\n\tprint(x)\n"],
	["directive in simd", "cannot contain", "#pragma native\nfunc f() -> void:\n\t#pragma omp simd\n\tfor i in 3:\n\t\t#pragma omp atomic # error\n\t\ti += 1\n"],
	["clause on the wrong directive", "unsupported OpenMP clause 'nowait' on parallel", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel nowait # error\n\t#pragma end parallel\n"],
	["nowait with reduction", "cannot be used with reduction", "#pragma native\nfunc f() -> void:\n\tvar x: int = 0\n\t#pragma omp for nowait reduction(+:x) # error\n\tfor i in 3:\n\t\tx += i\n"],
	["private member", "needs a local", "#pragma native\nvar a: int = 0\nfunc f() -> void:\n\t#pragma omp parallel for private(a)\n\tfor i in 3: # error\n\t\ta = i\n"],
	["unknown clause", "unsupported OpenMP clause", "#pragma native\nfunc f() -> void:\n\t#pragma omp parallel for nowait # error\n\tfor i in 3:\n\t\tprint(i)\n"],
	["member reduction", "needs a local", "#pragma native\nvar a: int = 0\nfunc f() -> void:\n\t#pragma omp parallel for reduction(+:a)\n\tfor i in 3: # error\n\t\ta += i\n"],
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
