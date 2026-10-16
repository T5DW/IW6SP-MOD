#include <Windows.h>
#include <cstdio>

#include "renderer.h"
#include "splash.h"

// Open a console/cmd window at launch, titled and showing a banner.
static void open_cmd_window()
{
    if (!AllocConsole())
        return;
    SetConsoleTitleA("IW6SP-MOD V1.1 By T5DW");

    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);

    std::printf("============================\n");
    std::printf(" IW6SP-MOD V1.1 By T5DW\n");
    std::printf("============================\n");
    std::printf("[+] DLL loaded. Initializing hooks...\n");
    std::fflush(stdout);
}

// Worker thread: do the hooking off the loader-lock so we never deadlock
// inside DllMain.
static DWORD WINAPI MainThread(LPVOID)
{
    open_cmd_window();
    Splash::Show();     // custom splash.bmp if present
    Renderer::Setup();
    std::printf("[+] Ready. Press your console key in-game.\n");
    std::fflush(stdout);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, MainThread, module, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        Renderer::Shutdown();
        break;
    }
    return TRUE;
}
