/**************************************************************************/
/*  extern_override.h                                                     */
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

// Contained space for function overrides when necessary for older OSs
// like Vista.

#ifndef EXTERN_OVERRIDE_H
#define EXTERN_OVERRIDE_H

extern "C"
{
	__declspec(dllexport) DWORD NvOptimusEnablement = 1;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
	__declspec(dllexport) void NoHotPatch() {} // Disable Nahimic code injection.

	// Function pointer types
	typedef BOOL (WINAPI *PFN_TryAcquireSRWLock)(PSRWLOCK);
	typedef DWORD (WINAPI *PFN_GetModuleBaseNameW)(HANDLE, HMODULE, LPWSTR, DWORD);
	typedef BOOL (WINAPI *PFN_GetModuleInformation)(HANDLE, HMODULE, LPMODULEINFO, DWORD);
	typedef BOOL (WINAPI *PFN_EnumProcessModulesEx)(HANDLE, HMODULE*, DWORD, LPDWORD, DWORD);

	// Thunk implementations
	// Will return the real function whenever possible so that
	// modern OSs don't pay the price for supporting Vista
	inline BOOL WINAPI Dynamic_TryAcquireSRWLockExclusive(PSRWLOCK SRWLock) {
		static PFN_TryAcquireSRWLock pRealFunc = (PFN_TryAcquireSRWLock)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TryAcquireSRWLockExclusive");
		if (pRealFunc) {
			return pRealFunc(SRWLock);
		}
		return FALSE; // Fallback
	}
	
	inline BOOL WINAPI Dynamic_TryAcquireSRWLockShared(PSRWLOCK SRWLock) {
		static PFN_TryAcquireSRWLock pRealFunc = (PFN_TryAcquireSRWLock)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TryAcquireSRWLockShared");
		if (pRealFunc) {
			return pRealFunc(SRWLock);
		}
		return FALSE; // Fallback
	}
	
	inline BOOL WINAPI Dynamic_K32EnumProcessModulesEx(
		HANDLE hProcess, 
		HMODULE* lphModule, 
		DWORD cb, 
		LPDWORD lpcbNeeded, 
		DWORD dwFilterFlag
	) {
		static PFN_EnumProcessModulesEx pRealFunc = (PFN_EnumProcessModulesEx)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32EnumProcessModulesEx");
		if (pRealFunc) {
			return pRealFunc(hProcess, lphModule, cb, lpcbNeeded, dwFilterFlag);
		}

		// Fallback via psapi.dll
		static PFN_EnumProcessModulesEx pFallbackFunc = nullptr;
		static bool fallback_init = false;
		
		if (!fallback_init) {
			HMODULE hPsapi = GetModuleHandleW(L"psapi.dll");
			if (!hPsapi) {
				hPsapi = LoadLibraryW(L"psapi.dll");
			}
			if (hPsapi) {
				pFallbackFunc = (PFN_EnumProcessModulesEx)GetProcAddress(hPsapi, "EnumProcessModulesEx");
			}
			fallback_init = true;
		}

		if (pFallbackFunc) {
			return pFallbackFunc(hProcess, lphModule, cb, lpcbNeeded, dwFilterFlag);
		}
		return FALSE;
	}
	
	inline DWORD WINAPI Dynamic_K32GetModuleBaseNameW(HANDLE hProcess, HMODULE hModule, LPWSTR lpBaseName, DWORD nSize) {
		static PFN_GetModuleBaseNameW pRealFunc = (PFN_GetModuleBaseNameW)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetModuleBaseNameW");
		if (pRealFunc) {
			return pRealFunc(hProcess, hModule, lpBaseName, nSize);
		}

		// Fallback via psapi.dll
		static PFN_GetModuleBaseNameW pFallbackFunc = nullptr;
		static bool fallback_init = false;
		
		if (!fallback_init) {
			HMODULE hPsapi = GetModuleHandleW(L"psapi.dll");
			if (!hPsapi) {
				hPsapi = LoadLibraryW(L"psapi.dll");
			}
			if (hPsapi) {
				pFallbackFunc = (PFN_GetModuleBaseNameW)GetProcAddress(hPsapi, "GetModuleBaseNameW");
			}
			fallback_init = true;
		}

		if (pFallbackFunc) {
			return pFallbackFunc(hProcess, hModule, lpBaseName, nSize);
		}
		return 0;
	}
	
	inline BOOL WINAPI Dynamic_K32GetModuleInformation(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb) {
		static PFN_GetModuleInformation pRealFunc = (PFN_GetModuleInformation)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetModuleInformation");
		if (pRealFunc) {
			return pRealFunc(hProcess, hModule, lpmodinfo, cb);
		}

		// Fallback via psapi.dll
		static PFN_GetModuleInformation pFallbackFunc = nullptr;
		static bool fallback_init = false;
		
		if (!fallback_init) {
			HMODULE hPsapi = GetModuleHandleW(L"psapi.dll");
			if (!hPsapi) {
				hPsapi = LoadLibraryW(L"psapi.dll");
			}
			if (hPsapi) {
				pFallbackFunc = (PFN_GetModuleInformation)GetProcAddress(hPsapi, "GetModuleInformation");
			}
			fallback_init = true;
		}

		if (pFallbackFunc) {
			return pFallbackFunc(hProcess, hModule, lpmodinfo, cb);
		}
		return FALSE;
	}

	// Intercept pointer paths (__declspec(dllimport) calls)
	BOOL (WINAPI *__imp_TryAcquireSRWLockExclusive)(PSRWLOCK) = Dynamic_TryAcquireSRWLockExclusive;
	BOOL (WINAPI *__imp_TryAcquireSRWLockShared)(PSRWLOCK) = Dynamic_TryAcquireSRWLockShared;
	BOOL (WINAPI *__imp_K32EnumProcessModulesEx)(HANDLE, HMODULE*, DWORD, LPDWORD, DWORD) = Dynamic_K32EnumProcessModulesEx;
	DWORD (WINAPI *__imp_K32GetModuleBaseNameW)(HANDLE, HMODULE, LPWSTR, DWORD) = Dynamic_K32GetModuleBaseNameW;
	BOOL (WINAPI *__imp_K32GetModuleInformation)(HANDLE, HMODULE, LPMODULEINFO, DWORD) = Dynamic_K32GetModuleInformation;

	// Intercept bare paths (direct, undecorated function calls)
	inline BOOL WINAPI K32EnumProcessModulesEx(HANDLE hProcess, HMODULE* lphModule, DWORD cb, LPDWORD lpcbNeeded, DWORD dwFilterFlag) {
		return Dynamic_K32EnumProcessModulesEx(hProcess, lphModule, cb, lpcbNeeded, dwFilterFlag);
	}

	inline DWORD WINAPI K32GetModuleBaseNameW(HANDLE hProcess, HMODULE hModule, LPWSTR lpBaseName, DWORD nSize) {
		return Dynamic_K32GetModuleBaseNameW(hProcess, hModule, lpBaseName, nSize);
	}

	inline BOOL WINAPI K32GetModuleInformation(HANDLE hProcess, HMODULE hModule, LPMODULEINFO lpmodinfo, DWORD cb) {
		return Dynamic_K32GetModuleInformation(hProcess, hModule, lpmodinfo, cb);
	}

} // extern "C"

#endif // EXTERN_OVERRIDE_H