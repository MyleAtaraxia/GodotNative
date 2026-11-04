#if defined(_WIN32) && defined(_OPENMP) && defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Worker threads stay parked inside vcomp140.dll, so unloading it with this library crashes on exit.
void gn_pin_openmp() {
	HMODULE module;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"vcomp140.dll", &module);
}
#else
void gn_pin_openmp() {}
#endif
