#include "Core/Application.h"
#include "Util/Log.h"
#include <Windows.h>
#include <shellapi.h>
#include <string>

int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPSTR lpCmdLine,
    _In_ int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    // Console output disabled — all logging goes through LOG_INFO (OutputDebugString)
    (void)lpCmdLine;

    LOG_INFO("=== War Times V0.01 ===");
    LOG_INFO("Starting up...");

    auto& app = WT::Application::Get();
    int argCount = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argCount);
    if (argv) {
        for (int i = 1; i < argCount; ++i) {
            std::wstring arg = argv[i];
            if ((arg == L"-level" || arg == L"--level") && (i + 1) < argCount) {
                ++i;
                char levelPath[MAX_PATH * 4] = {};
                WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, levelPath, static_cast<int>(sizeof(levelPath)), nullptr, nullptr);
                app.SetStartupLevelPath(levelPath);
                LOG_INFO("Startup level requested: %s", levelPath);
            }
        }
        LocalFree(argv);
    }

    if (!app.Init(hInstance, 1920, 1080)) {
        LOG_ERROR("Failed to initialize application!");
        MessageBox(nullptr, L"Failed to initialize application.", L"Error", MB_ICONERROR);
        return -1;
    }

    int result = app.Run();

    app.Shutdown();

    LOG_INFO("Exiting with code %d", result);

#ifdef _DEBUG
    FreeConsole();
#endif

    return result;
}
