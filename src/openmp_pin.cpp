#if defined(_WIN32) && defined(_OPENMP)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Worker threads stay parked inside libomp.dll, so unloading it with this library crashes on exit.
void gn_pin_openmp() {
	HMODULE module;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"libomp.dll", &module);
}
#else
void gn_pin_openmp() {}
#endif
