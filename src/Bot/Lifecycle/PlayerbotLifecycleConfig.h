#ifndef PLAYERBOTS_PLAYERBOTLIFECYCLECONFIG_H
#define PLAYERBOTS_PLAYERBOTLIFECYCLECONFIG_H

#include <string>
#include <vector>

struct PlayerbotLifecycleConfigValues
{
    bool cleanupRequested = false;
    std::string cleanupConfirmation;
    std::vector<std::string> protectedCharacters;
};

[[nodiscard]] std::vector<std::string> ParsePlayerbotLifecycleProtectedCharacters(std::string const& value);
void ReloadPlayerbotLifecycleConfig();

extern PlayerbotLifecycleConfigValues sPlayerbotLifecycleConfig;

#endif
