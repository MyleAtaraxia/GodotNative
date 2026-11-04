#pragma native
extends RefCounted

const VIEW: float = 60.0
const AVOID: float = 20.0
const MAX_SPEED: float = 260.0
const MIN_SPEED: float = 120.0
const DT: float = 0.0166

var count: int = 0
var width: float = 0.0
var height: float = 0.0
var rng: int = 12345
var px: PackedFloat64Array
var py: PackedFloat64Array
var vx: PackedFloat64Array
var vy: PackedFloat64Array
var nx: PackedFloat64Array
var ny: PackedFloat64Array
var out: PackedFloat64Array

func random() -> float:
	rng = (rng * 1103515245 + 12345) & 2147483647
	return float(rng) / 2147483648.0

func setup(p_count: int, p_width: float, p_height: float) -> void:
	count = p_count
	width = p_width
	height = p_height
	px.resize(count)
	py.resize(count)
	vx.resize(count)
	vy.resize(count)
	nx.resize(count)
	ny.resize(count)
	out.resize(count * 8)
	for i in range(count):
		px[i] = random() * width
		py[i] = random() * height
		vx[i] = (random() - 0.5) * MAX_SPEED
		vy[i] = (random() - 0.5) * MAX_SPEED

func step() -> void:
	var view2: float = VIEW * VIEW
	var avoid2: float = AVOID * AVOID
	#pragma omp parallel for
	for i in range(count):
		var x: float = px[i]
		var y: float = py[i]
		var seen: int = 0
		var cx: float = 0.0
		var cy: float = 0.0
		var ax: float = 0.0
		var ay: float = 0.0
		var sx: float = 0.0
		var sy: float = 0.0
		for j in range(count):
			var dx: float = px[j] - x
			var dy: float = py[j] - y
			var d2: float = dx * dx + dy * dy
			if j != i and d2 < view2:
				seen += 1
				cx += dx
				cy += dy
				ax += vx[j]
				ay += vy[j]
				if d2 < avoid2:
					sx -= dx
					sy -= dy
		var fx: float = vx[i]
		var fy: float = vy[i]
		if seen > 0:
			var inv: float = 1.0 / float(seen)
			fx += cx * inv * 0.6 + (ax * inv - fx) * 0.05 + sx * 1.2
			fy += cy * inv * 0.6 + (ay * inv - fy) * 0.05 + sy * 1.2
		var speed: float = sqrt(fx * fx + fy * fy)
		if speed > MAX_SPEED:
			fx = fx / speed * MAX_SPEED
			fy = fy / speed * MAX_SPEED
		elif speed < MIN_SPEED and speed > 0.0:
			fx = fx / speed * MIN_SPEED
			fy = fy / speed * MIN_SPEED
		nx[i] = fx
		ny[i] = fy
	for i in range(count):
		var fx: float = nx[i]
		var fy: float = ny[i]
		vx[i] = fx
		vy[i] = fy
		var x: float = px[i] + fx * DT
		var y: float = py[i] + fy * DT
		if x < 0.0:
			x += width
		elif x >= width:
			x -= width
		if y < 0.0:
			y += height
		elif y >= height:
			y -= height
		px[i] = x
		py[i] = y
		var speed: float = sqrt(fx * fx + fy * fy)
		var c: float = fx / speed
		var s: float = fy / speed
		var o: int = i * 8
		out[o] = c
		out[o + 1] = -s
		out[o + 3] = x
		out[o + 4] = s
		out[o + 5] = c
		out[o + 7] = y

func transforms() -> PackedFloat64Array:
	return out

func checksum() -> float:
	var total: float = 0.0
	for i in range(count):
		total += px[i] + py[i] * 3.0
	return total
