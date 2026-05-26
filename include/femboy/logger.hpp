#pragma once

#include "framework.hpp"

NAMESPACE_BEGIN(femboy)

class Logger
{
public:
    static Logger& Instance()
    {
        static Logger s_instance;
        return s_instance;
    }

    void Init(const wchar_t* path);
    void SetVerbose(bool v) { m_verbose = v; }
    bool IsVerbose() const { return m_verbose; }
    void Log(const char* format, ...);

private:
    Logger() : m_initialized(false), m_verbose(false), m_file(INVALID_HANDLE_VALUE) {}
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool m_initialized;
    bool m_verbose;
    HANDLE m_file;
};

NAMESPACE_END(femboy)
