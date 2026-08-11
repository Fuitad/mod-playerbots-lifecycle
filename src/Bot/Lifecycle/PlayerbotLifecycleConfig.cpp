#include "Bot/Lifecycle/PlayerbotLifecycleConfig.h"

#include <cctype>
#include <string_view>

#include "Config.h"

namespace
{
std::string Trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return std::string(value);
}
}  // namespace

PlayerbotLifecycleConfigValues sPlayerbotLifecycleConfig;

std::vector<std::string> ParsePlayerbotLifecycleProtectedCharacters(std::string const& value)
{
    std::vector<std::string> names;
    std::size_t begin = 0;
    while (begin <= value.size())
    {
        std::size_t const comma = value.find(',', begin);
        std::size_t const end = comma == std::string::npos ? value.size() : comma;
        std::string name = Trim(std::string_view(value).substr(begin, end - begin));
        if (!name.empty())
            names.push_back(std::move(name));
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return names;
}

void ReloadPlayerbotLifecycleConfig()
{
    PlayerbotLifecycleConfigValues values;
    values.cleanupRequested = sConfigMgr->GetOption<bool>("PlayerbotsLifecycle.CleanupRequested", false);
    values.cleanupConfirmation = sConfigMgr->GetOption<std::string>("PlayerbotsLifecycle.CleanupConfirmation", "");
    values.protectedCharacters = ParsePlayerbotLifecycleProtectedCharacters(
        sConfigMgr->GetOption<std::string>("PlayerbotsLifecycle.ProtectedCharacters", ""));
    sPlayerbotLifecycleConfig = std::move(values);
}
