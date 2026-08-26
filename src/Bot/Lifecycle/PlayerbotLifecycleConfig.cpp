#include "Bot/Lifecycle/PlayerbotLifecycleConfig.h"

#include <cctype>
#include <charconv>
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

PlayerbotLifecycleProtectedAccountList ParsePlayerbotLifecycleProtectedAccounts(std::string const& value)
{
    PlayerbotLifecycleProtectedAccountList parsed;
    std::size_t begin = 0;
    while (begin <= value.size())
    {
        std::size_t const comma = value.find(',', begin);
        std::size_t const end = comma == std::string::npos ? value.size() : comma;
        std::string const entry = Trim(std::string_view(value).substr(begin, end - begin));
        if (!entry.empty())
        {
            std::uint32_t accountId = 0;
            auto const result = std::from_chars(entry.data(), entry.data() + entry.size(), accountId);
            bool const complete = result.ec == std::errc() && result.ptr == entry.data() + entry.size();
            // Zero is never a real account id, so it counts as malformed rather than as an id.
            if (complete && accountId)
                parsed.accountIds.push_back(accountId);
            else
                parsed.malformed = true;
        }
        if (comma == std::string::npos)
            break;
        begin = comma + 1;
    }
    return parsed;
}

void ReloadPlayerbotLifecycleConfig()
{
    PlayerbotLifecycleConfigValues values;
    values.cleanupRequested = sConfigMgr->GetOption<bool>("PlayerbotsLifecycle.CleanupRequested", false);
    values.cleanupConfirmation = sConfigMgr->GetOption<std::string>("PlayerbotsLifecycle.CleanupConfirmation", "");
    PlayerbotLifecycleProtectedAccountList protectedAccounts = ParsePlayerbotLifecycleProtectedAccounts(
        sConfigMgr->GetOption<std::string>("PlayerbotsLifecycle.ProtectedAccounts", ""));
    values.protectedAccounts = std::move(protectedAccounts.accountIds);
    values.protectedAccountsMalformed = protectedAccounts.malformed;
    sPlayerbotLifecycleConfig = std::move(values);
}
