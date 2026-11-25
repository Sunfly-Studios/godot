extends SceneTree

const KEY = "p@$$w0rD".hash()
const VAL_42 = [0, [1, 42]].back().max()
const UPPER = "godot".to_upper()
const VEC_LEN = Vector2(3.0, 4.0).length()
const FROZEN_SLICE = [10, 20, 30, 40].slice(1, 3)

func _init():
	print("KEY matches: ", KEY == "p@$$w0rD".hash())
	print("VAL_42: ", VAL_42)
	print("UPPER: ", UPPER)
	print("VEC_LEN: ", VEC_LEN)
	print("FROZEN_SLICE: ", FROZEN_SLICE)
	print("FROZEN_SLICE read_only: ", FROZEN_SLICE.is_read_only())
	quit()