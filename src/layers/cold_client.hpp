#pragma once
#include "base.hpp"
#include <vector>

namespace femboy::patch {

class ColdClient : public LayerBase
{
public:
    const char* Name() override;
    bool Apply() override;

    static void restore_registry();

private:
    struct RegBackup
    {
        HKEY h_key;
        std::wstring sub_key;
        std::wstring value_name;
        std::vector<uint8_t> data;
        DWORD type;
    };
    static std::vector<RegBackup> s_reg_backups;
    static bool s_has_backup;

    bool backup_and_write(const wchar_t* sub_key, const wchar_t* value_name,
        DWORD type, const void* data, DWORD data_size);
};

}
