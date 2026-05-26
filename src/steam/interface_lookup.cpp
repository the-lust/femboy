#include "steam.hpp"
#include "logger.hpp"
#include <fstream>
#include <unordered_map>
#include <cstring>
#include <cstdlib>

namespace femboy { namespace steam {

InterfaceLookup g_interface_lookup;

static std::unordered_map<std::string, void*> g_handlers;
static std::mutex g_handler_mutex;

void register_handler(const char* method_name, void* handler)
{
    std::lock_guard<std::mutex> lock(g_handler_mutex);
    g_handlers[method_name] = handler;
}

void* get_handler(const char* method_name)
{
    std::lock_guard<std::mutex> lock(g_handler_mutex);
    auto it = g_handlers.find(method_name);
    if (it != g_handlers.end()) return it->second;
    return nullptr;
}

int __fastcall Stub_Return0(void*, int) { return 0; }
int __fastcall Stub_Return1(void*, int) { return 1; }

static const char g_empty_string[1] = { 0 };
const char* __fastcall Stub_ReturnEmptyString(void*, int) { return g_empty_string; }

uint32_t __fastcall Stub_ReturnUint32Max(void*, int) { return 0xFFFFFFFF; }

uint32_t __fastcall Stub_ReturnTimestamp(void*, int) { return (uint32_t)time(nullptr); }

int __fastcall Stub_ReturnLicenseYes(void*, int) { return 3; }

void* get_default_stub(const char* method_name)
{
    if (!method_name) return (void*)Stub_Return0;

    if (strstr(method_name, "Language") || strstr(method_name, "Name") ||
        strstr(method_name, "FilePath") || strstr(method_name, "Folder") ||
        strstr(method_name, "Path") || strstr(method_name, "Country") ||
        strstr(method_name, "IPCountry") || strstr(method_name, "Persona") ||
        strstr(method_name, "String"))
        return (void*)Stub_ReturnEmptyString;

    if (strncmp(method_name, "BIs", 3) == 0 || strncmp(method_name, "BHas", 4) == 0 ||
        strncmp(method_name, "BGet", 4) == 0 || strcmp(method_name, "BLoggedOn") == 0 ||
        strcmp(method_name, "BInit") == 0 || strcmp(method_name, "IsOverlayEnabled") == 0 ||
        strcmp(method_name, "IsSubscribed") == 0 || strcmp(method_name, "IsDlcInstalled") == 0 ||
        strcmp(method_name, "BIsSubscribedApp") == 0 || strcmp(method_name, "BIsDlcInstalled") == 0 ||
        strcmp(method_name, "BIsLowViolence") == 0 || strcmp(method_name, "BIsCybercafe") == 0)
        return (void*)Stub_Return1;

    if (strcmp(method_name, "UserHasLicenseForApp") == 0)
        return (void*)Stub_ReturnLicenseYes;

    if (strcmp(method_name, "GetServerRealTime") == 0)
        return (void*)Stub_ReturnTimestamp;

    return (void*)Stub_Return0;
}

bool InterfaceLookup::load_from_file(const wchar_t* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse_json(data.data(), data.size());
}

bool InterfaceLookup::load_from_resource(int res_id)
{
    HMODULE h_mod = GetModuleHandleW(L"femboy.dll");
    if (!h_mod) h_mod = GetModuleHandleW(nullptr);

    HRSRC h_res = FindResourceW(h_mod, MAKEINTRESOURCEW(res_id), L"RCDATA");
    if (!h_res) { LOG("InterfaceLookup: resource %d not found", res_id); return false; }

    HGLOBAL h_glob = LoadResource(h_mod, h_res);
    if (!h_glob) return false;

    const uint8_t* data = (const uint8_t*)LockResource(h_glob);
    DWORD size = SizeofResource(h_mod, h_res);
    if (!data || !size) return false;

    return parse_json(data, size);
}

// based json parser, not my best work ngl
bool InterfaceLookup::parse_json(const uint8_t* raw, size_t raw_size)
{
    std::string json((const char*)raw, raw_size);

    size_t pos = 0;
    pos = json.find('{');
    if (pos == std::string::npos) return false;
    pos++;

    while (pos < json.size())
    {
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
        if (pos >= json.size() || json[pos] == '}') break;

        if (json[pos] != '"') { LOG("InterfaceLookup: expected '\"' at pos %zu", pos); return false; }
        pos++;
        size_t key_start = pos;
        while (pos < json.size() && json[pos] != '"') pos++;
        if (pos >= json.size()) return false;
        std::string version_str = json.substr(key_start, pos - key_start);
        pos++;

        while (pos < json.size() && json[pos] != ':') pos++;
        if (pos >= json.size()) return false;
        pos++;

        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
        if (pos >= json.size() || json[pos] != '{') return false;
        pos++;

        InterfaceEntry entry;
        entry.version_string = version_str;

        while (pos < json.size())
        {
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
            if (pos >= json.size() || json[pos] == '}') break;

            if (json[pos] != '"') return false;
            pos++;
            size_t meth_start = pos;
            while (pos < json.size() && json[pos] != '"') pos++;
            std::string method_name = json.substr(meth_start, pos - meth_start);
            pos++;

            while (pos < json.size() && json[pos] != ':') pos++;
            pos++;

            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
            char* end = nullptr;
            int idx = (int)strtol(json.c_str() + pos, &end, 10);
            if (end) pos = end - json.c_str();

            MethodEntry me;
            me.name = method_name;
            me.index = (uint16_t)idx;
            entry.methods.push_back(me);

            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
            if (pos < json.size() && json[pos] == ',') pos++;
        }

        if (pos < json.size() && json[pos] == '}') pos++;
        m_entries[entry.version_string] = std::move(entry);

        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r' || json[pos] == '\t')) pos++;
        if (pos < json.size() && json[pos] == ',') pos++;
    }

    LOG("InterfaceLookup: loaded %zu interface versions from JSON", m_entries.size());
    return true;
}

const InterfaceEntry* InterfaceLookup::find(const char* version) const
{
    auto it = m_entries.find(version);
    if (it != m_entries.end()) return &it->second;
    return nullptr;
}

uint16_t InterfaceLookup::get_index(const char* version, const char* method) const
{
    auto* entry = find(version);
    if (!entry) return (uint16_t)-1;
    for (auto& m : entry->methods)
        if (m.name == method) return m.index;
    return (uint16_t)-1;
}

bool InterfaceLookup::has_interface(const char* version) const
{
    return m_entries.find(version) != m_entries.end();
}

void* InterfaceLookup::create_fake_vtable(const char* version, void* real)
{
    auto* entry = find(version);
    if (!entry)
    {
        LOG("create_fake_vtable: unknown interface %s", version);
        return nullptr;
    }

    uint16_t max_idx = 0;
    for (auto& m : entry->methods)
        if (m.index > max_idx) max_idx = m.index;
    size_t num_slots = (size_t)max_idx + 1;

    void** alloc = (void**)VirtualAlloc(nullptr, (num_slots + 1) * sizeof(void*),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!alloc) return nullptr;

    alloc[0] = (void*)(alloc + 1);

    void** vtable_slots = alloc + 1;

    if (real)
    {
        void** real_slots = (void**)real;
        for (size_t i = 0; i < num_slots; i++)
            vtable_slots[i] = real_slots[i + 1];
    }
    else
    {
        for (size_t i = 0; i < num_slots; i++)
            vtable_slots[i] = (void*)Stub_Return0;
    }

    for (auto& m : entry->methods)
    {
        if ((size_t)m.index < num_slots)
        {
            void* handler = get_handler(m.name.c_str());
            if (handler)
                vtable_slots[m.index] = handler;
            else
                vtable_slots[m.index] = get_default_stub(m.name.c_str());
        }
    }

    DWORD old;
    VirtualProtect(alloc, (num_slots + 1) * sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    m_allocated_vtables.push_back(alloc);

    LOG("create_fake_vtable: %s -> %p (%zu slots, %zu methods)", version, alloc, num_slots, entry->methods.size());
    return (void*)alloc;
}

void InterfaceLookup::free_fake_vtable(void* interface_obj)
{
    if (!interface_obj) return;
    auto it = std::find(m_allocated_vtables.begin(), m_allocated_vtables.end(), interface_obj);
    if (it != m_allocated_vtables.end())
    {
        VirtualFree(interface_obj, 0, MEM_RELEASE);
        m_allocated_vtables.erase(it);
    }
}

void InterfaceLookup::cleanup_all_fake_vtables()
{
    for (auto* vtable : m_allocated_vtables)
        VirtualFree(vtable, 0, MEM_RELEASE);
    m_allocated_vtables.clear();
    LOG("InterfaceLookup: cleaned up %zu fake vtables", m_allocated_vtables.size());
}

} }
