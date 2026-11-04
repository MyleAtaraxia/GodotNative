#pragma native
class_name Shape
extends RefCounted


func area() -> float:
	return 0.0


func describe() -> String:
	return "area %.1f" % area()
