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
        case RandomPlayerbotCleanupRefusal::ProtectedAccountsUnconfigured:
            return "protected_accounts_unconfigured";
        case RandomPlayerbotCleanupRefusal::ProtectedAccountsMalformed:
            return "protected_accounts_malformed";
        case RandomPlayerbotCleanupRefusal::ProtectedAccountUnresolved:
            return "protected_account_unresolved";
        case RandomPlayerbotCleanupRefusal::ProtectedAccountTargeted:
            return "protected_account_targeted";
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
    std::vector<RandomPlayerbotProtectedAccount> protectedAccounts = request.protectedAccounts;
    std::sort(protectedAccounts.begin(), protectedAccounts.end(),
              [](auto const& left, auto const& right) { return left.accountId < right.accountId; });
    /*
     * Counts are mixed so the encoding is self delimiting. Without them the stream is a flat run of
     * numbers and record boundaries are ambiguous: one account owning five characters serialises to
     * the same bytes as five character-less accounts whose ids happen to equal those values. That is
     * not a hash collision, it is the same input, so no strength of hash would separate them.
     */
    digest.Mix(static_cast<uint32>(protectedAccounts.size()));
    digest.Mix(static_cast<uint8>(request.protectedAccountsMalformed ? 1u : 0u));
    for (RandomPlayerbotProtectedAccount& protectedAccount : protectedAccounts)
    {
        SortAndDeduplicate(protectedAccount.characterGuids);
        digest.Mix(protectedAccount.accountId);
        digest.Mix(static_cast<uint8>(protectedAccount.exists ? 1u : 0u));
        digest.Mix(static_cast<uint32>(protectedAccount.characterGuids.size()));
        for (uint32 const guid : protectedAccount.characterGuids)
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
     * Checked before the per account rules, because an empty list makes all of them vacuous: every
     * loop below passes trivially and the pass would proceed with nothing guarded. The shipped
     * default is empty, so this is the state a fresh install is in, and an operator who supplied the
     * confirmation digest without ever configuring a protected account has authorised a delete they
     * did not think through. Refusing here is what keeps the empty default safe without writing one
     * server's account id into a configuration template every install receives.
     */
    // Checked before the empty test: a list of nothing but bad tokens parses to empty, and naming
    // the parse failure is more use to the operator than reporting it as never configured.
    if (request.protectedAccountsMalformed)
    {
        plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedAccountsMalformed;
        return plan;
    }

    if (request.protectedAccounts.empty())
    {
        plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedAccountsUnconfigured;
        return plan;
    }

    for (auto const& protectedAccount : request.protectedAccounts)
    {
        // A mistyped id matches no account. An account that exists but owns no character is
        // legitimate and guards nothing, so it is not by itself a refusal.
        if (!protectedAccount.exists)
        {
            plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedAccountUnresolved;
            return plan;
        }

        /*
         * The account id is the authority, checked before any character list.
         *
         * Testing only the characters would trust a second snapshot taken by a separate query, and
         * the database layer returns nothing both for "this account owns no characters" and for
         * "that query failed". A protected account inside the deletion cohort would then pass on an
         * empty list. It would not merely lose the captured guids either: account deletion re-reads
         * the account's characters at deletion time, so it would take characters that were never in
         * the snapshot this plan approved.
         */
        if (std::binary_search(plan.accountIds.begin(), plan.accountIds.end(), protectedAccount.accountId))
        {
            plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedAccountTargeted;
            return plan;
        }

        // Kept as a second line of defence. If a protected account's character ever appears in the
        // cohort without that account being targeted, the bookkeeping is wrong and nothing should
        // be deleted on it.
        for (uint32 const guid : protectedAccount.characterGuids)
        {
            if (std::binary_search(plan.characterGuids.begin(), plan.characterGuids.end(), guid))
            {
                plan.refusal = RandomPlayerbotCleanupRefusal::ProtectedAccountTargeted;
                return plan;
            }
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
