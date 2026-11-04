#pragma native
class_name Square
extends Shape

var side: float = 2.0


func area() -> float:
	return side * side


func describe() -> String:
	return "square " + super.describe()
