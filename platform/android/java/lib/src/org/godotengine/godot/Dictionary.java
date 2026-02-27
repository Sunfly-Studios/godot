/**************************************************************************/
/*  Dictionary.java                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

package org.godotengine.godot;

import java.util.HashMap;
import java.util.Set;

public class Dictionary extends HashMap<String, Object> {
	protected String[] keys_cache;

	public String[] get_keys() {
		// Optimised from the manual iteration.
		return keySet().toArray(new String[0]);
	}

	public Object[] get_values() {
		return values().toArray();
	}

	public void set_keys(String[] keys) {
		keys_cache = keys;
	}

	public void set_values(Object[] vals) {
		if (keys_cache == null || vals == null) {
			android.util.Log.e("GodotDictionary", "Desync: keys_cache or vals is null. Aborting.");
			keys_cache = null;
			return;
		}

		// Guard against mismatched array sizes causing out-of-bounds crashes
		int limit = Math.min(keys_cache.length, vals.length);
		if (keys_cache.length != vals.length) {
			android.util.Log.w("GodotDictionary", "Size mismatch between keys and values. Truncating.");
		}

		for (int i = 0; i < limit; i++) {
			put(keys_cache[i], vals[i]);
		}
		keys_cache = null;
	}
}
