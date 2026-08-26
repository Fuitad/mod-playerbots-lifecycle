#ifndef PLAYERBOTS_PLAYERBOTLIFECYCLECONFIG_H
#define PLAYERBOTS_PLAYERBOTLIFECYCLECONFIG_H

#include <cstdint>
#include <string>
#include <vector>

/*
 * A parsed account id list, with whether anything in it failed to parse.
 *
 * The validity flag is carried rather than discarded because dropping bad tokens is only safe when
 * EVERY token is bad. "157,158abc" would otherwise yield a nonempty {157}, which reaches no refusal
 * at all, and the operator receives a valid digest for a guard set half the size they wrote.
 */
struct PlayerbotLifecycleProtectedAccountList
{
    std::vector<std::uint32_t> accountIds;

    // Set when a non-empty token was not a valid nonzero account id. Empty tokens, as produced by a
    // trailing or doubled comma, are formatting rather than a mistake and do not set this.
    bool malformed = false;
};

struct PlayerbotLifecycleConfigValues
{
    bool cleanupRequested = false;
    std::string cleanupConfirmation;
    std::vector<std::uint32_t> protectedAccounts;
    bool protectedAccountsMalformed = false;
};

/*
 * Parses a comma separated account id list. A malformed or zero entry is never guessed at: it is
 * dropped from the ids AND recorded in `malformed`, so the cleanup can refuse rather than proceed
 * with a quietly smaller guard set.
 */
[[nodiscard]] PlayerbotLifecycleProtectedAccountList ParsePlayerbotLifecycleProtectedAccounts(std::string const& value);
void ReloadPlayerbotLifecycleConfig();

extern PlayerbotLifecycleConfigValues sPlayerbotLifecycleConfig;

#endif
