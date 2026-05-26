#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>

HMODULE g_realVersionDll = nullptr;
HMODULE g_femboyDll = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);

        // Load real version.dll from System32
        wchar_t sysPath[MAX_PATH];
        GetSystemDirectoryW(sysPath, MAX_PATH);
        wcscat_s(sysPath, L"\\version.dll");
        g_realVersionDll = LoadLibraryW(sysPath);
        if (!g_realVersionDll)
        {
            // Fallback to search order
            g_realVersionDll = LoadLibraryW(L"version.dll");
        }

        // Load femboy.dll from same directory as this proxy
        wchar_t dllPath[MAX_PATH];
        GetModuleFileNameW(hModule, dllPath, MAX_PATH);
        std::wstring dir = dllPath;
        size_t pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
            dir = dir.substr(0, pos + 1);
        else
            dir.clear();

        std::wstring femboyPath = dir + L"femboy.dll";
        g_femboyDll = LoadLibraryW(femboyPath.c_str());
        if (!g_femboyDll)
        {
            // Try load_dlls subfolder
            femboyPath = dir + L"load_dlls\\femboy.dll";
            g_femboyDll = LoadLibraryW(femboyPath.c_str());
        }

        break;
    }
    case DLL_PROCESS_DETACH:
        if (g_femboyDll)
            FreeLibrary(g_femboyDll);
        if (g_realVersionDll)
            FreeLibrary(g_realVersionDll);
        break;
    }
    return TRUE;
}

// Forwarding helpers
#define FORWARD_FUNC(name) \
    extern "C" __declspec(naked) void __cdecl name() \
    { \
        __asm { jmp g_realVersionDll } \
    }

// Since __declspec(naked) with inline asm is MSVC x86 only, we use a different approach.
// We define each function and look up the real function at call time.

#define PROXY_FUNC(retType, callConv, name, params, args) \
    extern "C" retType callConv name params \
    { \
        static decltype(&name) realFunc = nullptr; \
        if (!realFunc && g_realVersionDll) \
            realFunc = reinterpret_cast<decltype(realFunc)>(GetProcAddress(g_realVersionDll, #name)); \
        if (realFunc) return realFunc args; \
        return 0; \
    }

PROXY_FUNC(BOOL, WINAPI, GetFileVersionInfoA, (LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (lptstrFilename, dwHandle, dwLen, lpData))

PROXY_FUNC(BOOL, WINAPI, GetFileVersionInfoByHandle, (DWORD dwHandle, LPCSTR lptstrFilename, DWORD dwLen, LPVOID lpData),
    (dwHandle, lptstrFilename, dwLen, lpData))

PROXY_FUNC(BOOL, WINAPI, GetFileVersionInfoExA, (DWORD dwFlags, LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (dwFlags, lptstrFilename, dwHandle, dwLen, lpData))

PROXY_FUNC(BOOL, WINAPI, GetFileVersionInfoExW, (DWORD dwFlags, LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (dwFlags, lptstrFilename, dwHandle, dwLen, lpData))

PROXY_FUNC(DWORD, WINAPI, GetFileVersionInfoSizeA, (LPCSTR lptstrFilename, LPDWORD lpdwHandle),
    (lptstrFilename, lpdwHandle))

PROXY_FUNC(DWORD, WINAPI, GetFileVersionInfoSizeExA, (DWORD dwFlags, LPCSTR lptstrFilename, LPDWORD lpdwHandle),
    (dwFlags, lptstrFilename, lpdwHandle))

PROXY_FUNC(DWORD, WINAPI, GetFileVersionInfoSizeExW, (DWORD dwFlags, LPCWSTR lptstrFilename, LPDWORD lpdwHandle),
    (dwFlags, lptstrFilename, lpdwHandle))

PROXY_FUNC(DWORD, WINAPI, GetFileVersionInfoSizeW, (LPCWSTR lptstrFilename, LPDWORD lpdwHandle),
    (lptstrFilename, lpdwHandle))

PROXY_FUNC(BOOL, WINAPI, GetFileVersionInfoW, (LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen, LPVOID lpData),
    (lptstrFilename, dwHandle, dwLen, lpData))

PROXY_FUNC(DWORD, WINAPI, VerFindFileA, (DWORD uFlags, LPCSTR szFileName, LPCSTR szWinDir, LPCSTR szAppDir, LPSTR szCurDir, PUINT lpuCurDirLen, LPSTR szDestDir, PUINT lpuDestDirLen),
    (uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen))

PROXY_FUNC(DWORD, WINAPI, VerFindFileW, (DWORD uFlags, LPCWSTR szFileName, LPCWSTR szWinDir, LPCWSTR szAppDir, LPWSTR szCurDir, PUINT lpuCurDirLen, LPWSTR szDestDir, PUINT lpuDestDirLen),
    (uFlags, szFileName, szWinDir, szAppDir, szCurDir, lpuCurDirLen, szDestDir, lpuDestDirLen))

PROXY_FUNC(DWORD, WINAPI, VerInstallFileA, (DWORD uFlags, LPCSTR szSrcFileName, LPCSTR szDestFileName, LPCSTR szSrcDir, LPCSTR szDestDir, LPCSTR szCurDir, LPSTR szTmpFile, PUINT lpuTmpFileLen),
    (uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen))

PROXY_FUNC(DWORD, WINAPI, VerInstallFileW, (DWORD uFlags, LPCWSTR szSrcFileName, LPCWSTR szDestFileName, LPCWSTR szSrcDir, LPCWSTR szDestDir, LPCWSTR szCurDir, LPWSTR szTmpFile, PUINT lpuTmpFileLen),
    (uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir, szCurDir, szTmpFile, lpuTmpFileLen))

PROXY_FUNC(DWORD, WINAPI, VerLanguageNameA, (DWORD wLang, LPSTR szLang, DWORD cchLang),
    (wLang, szLang, cchLang))

PROXY_FUNC(DWORD, WINAPI, VerLanguageNameW, (DWORD wLang, LPWSTR szLang, DWORD cchLang),
    (wLang, szLang, cchLang))

PROXY_FUNC(BOOL, WINAPI, VerQueryValueA, (LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen),
    (pBlock, lpSubBlock, lplpBuffer, puLen))

PROXY_FUNC(BOOL, WINAPI, VerQueryValueW, (LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer, PUINT puLen),
    (pBlock, lpSubBlock, lplpBuffer, puLen))
