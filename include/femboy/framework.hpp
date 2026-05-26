#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// based windows headers lets go
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <winternl.h>

#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <algorithm>
#include <atomic>

#include "defines.hpp"

// convienence alias — calls Logger::Instance().Log
#define LOG(...) ::femboy::Logger::Instance().Log(__VA_ARGS__)
