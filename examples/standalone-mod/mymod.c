/* Example Standalone Mod for The Return of the King / Athelas
 * -------------------------------------------------------------
 * This mod compiles into its own independent DLL (`MyMod.dll`).
 * To install it, simply drop it into:
 *   `<GameFolder>\ROTKFix\mods\MyMod.dll`
 *
 * ROTK Fix will automatically load it on game startup via `LoadLibraryA`,
 * without needing any modifications to the Athelas source code!
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

/* Exported optional initializer called by ROTK Fix */
__declspec(dllexport) void ModInit(void)
{
    OutputDebugStringA("[MyMod] ModInit called successfully from ROTKFix\\mods\\!\n");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)lpvReserved;

    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        OutputDebugStringA("[MyMod] Standalone Mod loaded into rotk.exe process!\n");
        /* You can perform any in-memory hooks, patches, or thread spawns here! */
    }

    return TRUE;
}
