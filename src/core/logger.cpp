#include "logger.hpp"
#include <cstdarg>

NAMESPACE_BEGIN(femboy)

Logger::~Logger()
{
    if (m_file != INVALID_HANDLE_VALUE)
        CloseHandle(m_file);
}

void Logger::Init(const wchar_t* path)
{
    // lowkey we logging everything fr fr
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(NULL, exe_path, MAX_PATH);
    wchar_t* slash = wcsrchr(exe_path, L'\\');
    if (slash) *(slash + 1) = L'\0';
    wcscat_s(exe_path, MAX_PATH, path);

    m_file = CreateFileW(exe_path, FILE_APPEND_DATA,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    m_initialized = true;

    Log("Logger initialized");
}

void Logger::Log(const char* format, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf) - 1, format, args);
    va_end(args);
    buf[sizeof(buf) - 1] = '\0';

    SYSTEMTIME st;
    GetLocalTime(&st);
    char time_buf[64];
    snprintf(time_buf, sizeof(time_buf), "[%02u:%02u:%02u.%03u] ",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    char out[1152];
    snprintf(out, sizeof(out), "%s%s\n", time_buf, buf);
    OutputDebugStringA(out);

    if (m_file != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(m_file, out, (DWORD)strlen(out), &written, nullptr);
    }
}

NAMESPACE_END(femboy)
