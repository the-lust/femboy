#pragma once
#include "framework.hpp"
#include <vector>
#include <unordered_map>
#include <string>

namespace femboy { namespace steam {

using SteamApiHandler = void*;

struct MethodEntry
{
    std::string name;
    uint16_t index;
};

struct InterfaceEntry
{
    std::string version_string;
    std::vector<MethodEntry> methods;
};

class InterfaceLookup
{
public:
        bool load_from_resource(int res_id);
    bool load_from_file(const wchar_t* path);
    const InterfaceEntry* find(const char* version) const;
    uint16_t get_index(const char* version, const char* method) const;
    bool has_interface(const char* version) const;
    void* create_fake_vtable(const char* version, void* real = nullptr);
    void free_fake_vtable(void* interface_obj);
    void cleanup_all_fake_vtables();
    size_t get_count() const { return m_entries.size(); }

private:
    std::unordered_map<std::string, InterfaceEntry> m_entries;
    std::vector<void*> m_allocated_vtables;
    bool parse_json(const uint8_t* data, size_t size);
};

extern InterfaceLookup g_interface_lookup;

void register_handler(const char* method_name, void* handler);
void* get_handler(const char* method_name);
void* get_default_stub(const char* method_name);

} }
