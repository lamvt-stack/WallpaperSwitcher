#include <exception>

#include "app.h"
#include "wic.h"
#include "win_util.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // Hardening, before anything else loads code: terminate on heap corruption, and never
    // search the application directory / PATH for DLLs (prevents DLL planting).
    ::HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    ws::Check(::SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32), L"SetDefaultDllDirectories");

    // Single instance. GetLastError() is read straight after the call.
    ws::UniqueHandle mutex(::CreateMutexW(nullptr, FALSE, L"Local\\WallpaperSwitcher_SingleInstance"));
    const DWORD mutexError = ::GetLastError();
    if (!mutex) {
        ws::LogError(L"CreateMutexW", mutexError);
        return 1;
    }
    if (mutexError == ERROR_ALREADY_EXISTS) return 0;

    ws::ComApartment com;                       // IDesktopWallpaper, WIC, file dialog (single-threaded apartment)
    if (!com.Ok()) {
        ws::LogError(L"CoInitializeEx", static_cast<DWORD>(com.Result()));
        return 1;
    }

    int exitCode = 1;
    try {
        ws::App app;
        if (app.Create(instance)) {
            app.Start();
            exitCode = app.Run();
        }
    } catch (const std::exception&) {
        ws::LogError(L"unhandled C++ exception", ERROR_INTERNAL_ERROR);
    }
    ws::ReleaseWic();                           // COM objects must go before CoUninitialize (ComApartment, below)
    return exitCode;
}
