extends SceneTree

const FEATURE_A = true
const FEATURE_B = false
const CONFIG = { "version": 4, "tag": "dev" }

func test_no_else() -> String:
	var result = "ok"
	if FEATURE_B:
		non_existent_call()
		result = "bad"
	return result

func test_mixed_elif(runtime_flag: bool) -> String:
	# Dead constant `if` falling through to a runtime `elif`.
	if FEATURE_B:
		non_existent_call()
	elif runtime_flag:
		return "runtime_true"
	else:
		return "runtime_false"

func test_short_circuit_elif() -> String:
	# Taken constant `if` must skip analyzing the `elif` condition too.
	if FEATURE_A:
		return "taken_first"
	elif non_existent_condition():
		return "bad"
	else:
		return "bad"

func test_truthiness() -> Array:
	var out = []
	if "":
		bad_empty_string()
	else:
		out.append("empty_str_false")

	if "hello".to_upper():
		out.append("str_true")

	if [1, 2].slice(0, 0):
		bad_empty_array()
	else:
		out.append("empty_arr_false")

	if 0:
		bad_zero()
	elif 42:
		out.append("int_true")

	return out

func test_locals_and_scoping() -> int:
	var x = 10
	if FEATURE_B:
		var y: NonExistentType = broken_init()
		x += y
	else:
		var y: int = 5
		x += y
	return x

func test_loops() -> Array:
	var out = []
	for i in range(5):
		if FEATURE_B:
			break
		if i == 2:
			if FEATURE_A:
				continue
		out.append(i)
	return out

func test_lambdas() -> int:
	var base = 100
	if FEATURE_B:
		var dead_fn = func():
			return non_existent_symbol + base
		return dead_fn.call()
	else:
		var live_fn = func(n: int) -> int:
			return base + n
		return live_fn.call(23)

func test_nested_and_folded() -> String:
	if CONFIG.has("version"):
		if CONFIG.get("tag").begins_with("d"):
			return "nested_ok"
		else:
			broken_inner()
	else:
		broken_outer()
	return "unreachable"

func _init():
	print("1. no_else: ", test_no_else())
	print("2. mixed_elif(true): ", test_mixed_elif(true))
	print("3. mixed_elif(false): ", test_mixed_elif(false))
	print("4. short_circuit_elif: ", test_short_circuit_elif())
	print("5. truthiness: ", test_truthiness())
	print("6. locals: ", test_locals_and_scoping())
	print("7. loops: ", test_loops())
	print("8. lambdas: ", test_lambdas())
	print("9. nested_and_folded: ", test_nested_and_folded())
	quit()