extends SceneTree

const MODE = "release"
const ENABLE_EXTRA = false

func get_value() -> int:
	if MODE.to_upper() == "RELEASE":
		return 42
	else:
		# Dead branch: invalid symbol and wrong return type must be skipped.
		non_existent_function_call()
		return "not an int"

func _init():
	print("get_value: ", get_value())

	if ENABLE_EXTRA:
		another_missing_symbol.do_something()
	elif [1, 2, 3].size() == 3:
		print("folded elif taken")
	else:
		yet_another_missing_symbol()

	quit()