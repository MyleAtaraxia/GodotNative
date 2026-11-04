#pragma native
class_name Calc
extends RefCounted

const LIMIT: int = 10

enum Mode { ADD, MUL }

var total: int = 0


func add(a: int, b: int) -> int:
	return a + b


func fib(n: int) -> int:
	if n < 2:
		return n
	return fib(n - 1) + fib(n - 2)


func sum_to(n: int) -> int:
	var sum: int = 0
	for i in range(1, n + 1):
		sum += i
	return sum


func divide(a: int, b: int) -> int:
	return a / b


func mix(a: int, b: float) -> float:
	return a * b


func apply(mode: int, a: int, b: int) -> int:
	if mode == Mode.MUL:
		return a * b
	return a + b


func length(v: Vector2) -> float:
	return v.length()


func bump() -> int:
	total += 1
	return total


func grade(score: int) -> String:
	match score:
		100:
			return "perfect"
		0:
			return "none"
	return "some"
