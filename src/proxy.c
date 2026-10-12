/* ROTK Fix forwarder: every export is forwarded to ROTKFix.dll (see .def). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE i, DWORD r, LPVOID p) { (void)i; (void)r; (void)p; return TRUE; }
