/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "Bot/Lifecycle/RandomPlayerbotCleanupPlan.h"

#include <algorithm>

#include "Util.h"

namespace
{
/*
 * Whether this is a name the factory itself would have produced.
 *
 * The factory renders `prefix` followed by a plain decimal counter, then AccountMgr applies its
 * Latin uppercase normalization before storing the name. The check applies that same normalization
 * and uses equality rather than a pattern match. A padded spelling is deliberately rejected even
 * though it parses to a number in range: the factory never writes one, so an account named that way
 * was named by something else, and something else's account is not ours to delete.
 */
[[nodiscard]] bool IsGeneratedAccountName(std::string const& name, std::string const& prefix, uint32 generatedCount)
{
    std::string storedName = name;
    std::string storedPrefix = prefix;
    if (!Utf8ToUpperOnlyLatin(storedName) || !Utf8ToUpperOnlyLatin(storedPrefix) || storedPrefix.empty() ||
        storedName.size() <= storedPrefix.size())
        return false;

    if (storedName.compare(0, storedPrefix.size(), storedPrefix) != 0)
        return false;

    std::string const suffix = storedName.substr(storedPrefix.size());
    if (suffix.find_first_not_of("0123456789") != std::string::npos)
        return false;

    // Bounded before conversion so a long digit run cannot overflow the parse.
    if (suffix.size() > 10)
        return false;

    uint64 const number = std::stoull(suffix);
    if (std::to_string(number) != suffix)
        return false;

    return number < static_cast<uint64>(generatedCount);
}

/*
 * FNV-1a over the exact target.
 *
 * This is a transcription check, not an authorization token. It exists so an operator cannot
 * confirm a target they never saw: a value copied from an older preview, or from a different
 * server, will not match. Whoever can supply it already holds the configuration file, so there
 * is no adversary to be collision resistant against, and a cryptographic digest here would buy
 * nothing while making the value harder to copy by hand.
 */
class TargetDigest
{
public:
    void Mix(uint8 byte)
    {
        _hash ^= static_cast<uint64>(byte);
        _hash *= 1099511628211ull;
    }

    void Mix(std::string const& text)
    {
        for (char const symbol : text)
            Mix(static_cast<uint8>(symbol));

        Mix(static_cast<uint8>(0));
    }

    void Mix(uint32 value)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
            Mix(static_cast<uint8>((value >> shift) & 0xFFu));
    }

    [[nodiscard]] std::string Render() const
    {
        static char const* const DIGITS = "0123456789abcdef";

        std::string text;
        text.reserve(16);
        for (int shift = 60; shift >= 0; shift -= 4)
            text.push_back(DIGITS[(_hash >> shift) & 0xFull]);

        return text;
    }

private:
    uint64 _hash = 1469598103934665603ull;
};

void SortAndDeduplicate(std::vector<uint32>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}
}  // namespace

char const* RandomPlayerbotCleanupRefusalName(RandomPlayerbotCleanupRefusal refusal)
{
    switch (refusal)
    {
        case RandomPlayerbotCleanupRefusal::None:
            return "none";
        case RandomPlayerbotCleanupRefusal::AccountNameOwnershipMismatch:
            return "account_name_ownership_mismatch";
        case RandomPlayerbotCleanupRefusal::ProtectedCharactersUnconfigured:
            return "protected_characters_unconfigured";
        case RandomPlayerbotCleanupRefusal::ProtectedCharacterUnresolved:
            return "protected_character_unresolved";
        case RandomPlayerbotCleanupRefusal::ProtectedCharacterAmbiguous:
            return "protected_character_ambiguous";
        case RandomPlayerbotCleanupRefusal::ProtectedCharacterTargeted:
            return "protected_character_targeted";
        case RandomPlayerbotCleanupRefusal::ConfirmationMissing:
            return "confirmation_missing";
        case RandomPlayerbotCleanupRefusal::ConfirmationMismatch:
            return "confirmation_mismatch";
    }

    return "unknown";
}

RandomPlayerbotCleanupPlan RandomPlayerbotBuildCleanupPlan(RandomPlayerbotCleanupRequest const& request)
{
    RandomPlayerbotCleanupPlan plan;
    std::map<uint32, RandomPlayerbotCandidateAccount const*> selectedAccounts;
    std::map<uint32, std::string> selectedCharacterNames;

    /*
     * Two authorities decide whether an account is ours: the name the factory would have generated,
     * and the ownership row the manager writes. Agreement includes it. Disagreement in either
     * direction refuses the whole pass rather than quietly shrinking the cohort, because a name
     * without its row, or a row on a name we never generated, means the bookkeeping no longer
     * describes reality, and that is the worst possible moment to trust a bulk delete.
     *
     * An account that neither authority claims, such as a human account that merely begins with the
     * same characters, is simply not ours. It is excluded without comment. That exclusion is the
     * whole point of the change: the `username LIKE 'prefix%'` predicate this replaces would have
     * deleted it.
     */
    bool mismatched = false;
    for (auto const& candidate : request.candidates)
    {
        bool const nameIsGenerated =
            IsGeneratedAccountName(candidate.accountName, request.accountNamePrefix, request.generatedAccountCount);

        if (nameIsGenerated != candidate.hasRandomBotOwnershipRow)
        {
            mismatched = true;
            continue;
        }

        if (!nameIsGenerated)
            continue;

        selectedAccounts.emplace(candidate.accountId, &candidate);
        for (uint32 const guid : candidate.characterGuids)
        {
            auto const name = candidate.characterNames.find(guid);
            if (name == candidate.characterNames.end() || name->second.empty())
                mismatched = true;

            selectedCharacterNames.emplace(guid, name == candidate.characterNames.end() ? "" : name->second);
        }

        plan.accountIds.push_back(candidate.accountId);
        plan.characterGuids.insert(plan.characterGuids.end(), candidate.characterGuids.begin(),
                                   candidate.characterGuids.end());
    }

    SortAndDeduplicate(plan.accountIds);
    SortAndDeduplicate(plan.characterGuids);

    TargetDigest digest;
    digest.Mix(request.accountNamePrefix);
    for (uint32 const accountId : plan.accountIds)
    {
        digest.Mix(accountId);
        RandomPlayerbotCandidateAccount const& account = *selectedAccounts.at(accountId);
        digest.Mix(account.accountName);
        digest.Mix(static_cast<uint8>(account.hasRandomBotOwnershipRow ? 1 : 0));
    }

    // Separates the two lists, so moving an identifier from one to the other changes the digest.
    digest.Mix(static_cast<uint8>(0xFFu));

    for (uint32 const guid : plan.characterGuids)
    {
        digest.Mix(guid);
        digest.Mix(selectedCharacterNames.at(guid));
    }

    // Protection is part of what the operator approves too. A changed name resolution must make an
    // older digest unusable even when the deletion cohort itself happened to stay the same.
    digest.Mix(static_cast<uint8>(0xFEu));
    std::vector<RandomPlayerbotProtectedCharacter> protectedCharacters = request.protectedCharacters;
    std::sort(protectedCharacters.begin(), protectedCharacters.end(),
              [](auto const& left, auto const& right) { return left.configuredName < right.configuredName; });
    for (RandomPlayerbotProtectedCharacter& protectedCharacter : protectedCharacters)
    {
        SortAndDeduplicate(protectedCharacter.resolvedGuids);
        digest.Mix(protectedCharacter.configuredName);
        for (uint32 const guid : protectedCharacter.resolvedGuids)
            digest.Mix(guid);
    }

    plan.confirmationDigest = digest.Render();

    // The complete target is reported even when the plan refuses, so a preview is accurate about
    // what a confirmed run would have done.
    if (mismatched)
    {
        plan.refusal = RandomPlayerbotCleanupRefusal::AccountNameOwnershipMismatch;
        return plan;
    }

    /*
     * Checked before the per character rules, because an empty list makes all of them vacuous: every
     * loop below passes trivially and the pass would proceed with nothing guarded. The shipped
     * default is empty, so this is the state a fresh install is in, and an operator who supplied the
     * confirmation digest without ever configuring a protected name has authorised a delete they did
     * not think through. Refusing here is what keeps the empty default safe without writing one
     * server's character name into a configuration template every install receives.
     */
    if (request.protectedCharacters.empty())
    {
        plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedCharactersUnconfigured;
        return plan;
    }

    for (auto const& protectedCharacter : request.protectedCharacters)
    {
        if (protectedCharacter.resolvedGuids.empty())
        {
            plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedCharacterUnresolved;
            return plan;
        }

        if (protectedCharacter.resolvedGuids.size() > 1)
        {
            plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedCharacterAmbiguous;
            return plan;
        }

        if (std::binary_search(plan.characterGuids.begin(), plan.characterGuids.end(),
                               protectedCharacter.resolvedGuids.front()))
        {
            plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedCharacterTargeted;
            return plan;
        }
    }

    if (request.suppliedConfirmation.empty())
    {
        plan.refusal = RandomPlayerbotCleanupRefusal::ConfirmationMissing;
        return plan;
    }

    if (request.suppliedConfirmation != plan.confirmationDigest)
    {
        plan.refusal = RandomPlayerbotCleanupRefusal::ConfirmationMismatch;
        return plan;
    }

    return plan;
}

bool RandomPlayerbotCleanupPlansAgree(RandomPlayerbotCleanupPlan const& captured,
                                      RandomPlayerbotCleanupPlan const& rebuilt)
{
    // A rebuild that refuses disagrees even when it names the same identifiers. Something changed
    // between the confirmation and the delete, and what changed is exactly what must be looked at.
    if (!captured.MayMutate() || !rebuilt.MayMutate())
        return false;

    return captured.accountIds == rebuilt.accountIds && captured.characterGuids == rebuilt.characterGuids &&
           captured.confirmationDigest == rebuilt.confirmationDigest;
}
