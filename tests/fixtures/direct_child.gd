#pragma native
class_name DirectChild
extends DirectBase


var extra: int = 100


func number() -> int:
	return super.number() + extra
