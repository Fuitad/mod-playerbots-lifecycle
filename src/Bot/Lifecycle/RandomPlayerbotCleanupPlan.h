/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef PLAYERBOTS_RANDOMPLAYERBOTCLEANUPPLAN_H
#define PLAYERBOTS_RANDOMPLAYERBOTCLEANUPPLAN_H

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "Define.h"

/*
 * The immutable target of a random bot cleanup, decided before anything is deleted.
 *
 * The path this replaces selected its victims with `username LIKE 'RNDbot%'` at the moment of
 * deletion. Two properties of that made it dangerous: the predicate was evaluated by the database
 * during the destructive statement, so nothing could be inspected first, and a `LIKE` pattern
 * matches any account a human happened to name with the same leading characters. This header turns
 * selection into a value that can be printed, compared, and refused, and leaves the destructive
 * statements with nothing to decide: they delete the exact identifiers captured here.
 *
 * Everything below is pure. It performs no query and holds no live object, so the rules that decide
 * who is deleted are provable by unit tests rather than only by running the destructive pass.
 */

// Why a cleanup must not proceed. Exactly one reason is reported, and any reason at all means no
// mutation happens: there is no partial cleanup.
enum class RandomPlayerbotCleanupRefusal : uint8
{
    None = 0,

    /*
     * The two authorities disagree about one account. Either an account carrying an exact generated
     * name has no random bot ownership row, or an account holding an ownership row is not named like
     * one this module generates. Both directions mean the bookkeeping no longer describes reality,
     * which is the last state in which a bulk delete should be trusted to pick its own targets.
     */
    AccountNameOwnershipMismatch,

    /*
     * No protected account is configured at all.
     *
     * An empty list is not a statement that nothing needs protecting; it is the shipped default,
     * which means the operator has not yet said anything on the subject. Supplying a confirmation
     * digest while leaving it empty would otherwise authorise a bulk delete with nothing guarded,
     * and the account that most needs guarding is the operator's own.
     *
     * The alternative, shipping a real account id as the default, was rejected: that id belongs to
     * one server and would be wrong in every other install of this module.
     */
    ProtectedAccountsUnconfigured,

    /*
     * The configured list contained a token that is not a valid nonzero account id.
     *
     * Dropping bad tokens silently is only safe when every token is bad, because one surviving
     * valid id keeps the list nonempty and no other refusal fires. The operator would then get a
     * digest authorising a guard set smaller than the one they wrote, and the account they meant to
     * protect with the mistyped entry is unguarded.
     */
    ProtectedAccountsMalformed,

    // A configured protected account id matched no account. Fails closed rather than assuming the
    // id was stale, because the alternative is deleting the characters it was meant to protect.
    ProtectedAccountUnresolved,

    // A protected account owns a character inside the computed cohort. This is the guard that keeps
    // a real player's characters out of a bot sweep.
    ProtectedAccountTargeted,

    // No confirmation was supplied. The complete target is still computed so it can be previewed;
    // nothing is deleted.
    ConfirmationMissing,

    // A confirmation was supplied and does not match this target. The cohort changed since it was
    // read, or the value was copied from a different run.
    ConfirmationMismatch
};

inline constexpr std::size_t PLAYERBOT_RANDOM_CLEANUP_REFUSAL_COUNT = 8;

[[nodiscard]] char const* RandomPlayerbotCleanupRefusalName(RandomPlayerbotCleanupRefusal refusal);

// One account offered for consideration, with both authorities that decide whether it is ours.
struct RandomPlayerbotCandidateAccount
{
    uint32 accountId = 0;

    // The account's exact stored name. Compared for equality against the names this module
    // generates, never with a pattern.
    std::string accountName;

    // Whether `playerbots_account_type` holds a random bot ownership row for this account.
    bool hasRandomBotOwnershipRow = false;

    std::vector<uint32> characterGuids;

    // Names captured beside the GUIDs so the preview shows the same database snapshot the digest
    // authorizes. Missing entries remain visible as missing rather than being guessed later.
    std::map<uint32, std::string> characterNames;
};

/*
 * One configured protected account and every character it owns.
 *
 * Protection is account scoped rather than character scoped on purpose. A name list guards only the
 * characters someone remembered to write down, so a character created later on the same account is
 * unguarded until the configuration is edited again. An account id covers every character it owns,
 * including ones that do not exist yet, which is the property that makes the guard hold over time.
 *
 * `exists` is carried separately from the character list because an account with no characters is
 * legitimate, while an id that matches no account at all is a typo. Collapsing the two would turn
 * a mistyped id into a silently unguarded run.
 */
struct RandomPlayerbotProtectedAccount
{
    uint32 accountId = 0;
    bool exists = false;
    std::vector<uint32> characterGuids;
};

struct RandomPlayerbotCleanupRequest
{
    // The configured account name prefix and how many accounts the factory generates from it. The
    // exact names are derived from these two, so a name outside that set is not one of ours no
    // matter what it begins with.
    std::string accountNamePrefix;
    uint32 generatedAccountCount = 0;

    std::vector<RandomPlayerbotCandidateAccount> candidates;
    std::vector<RandomPlayerbotProtectedAccount> protectedAccounts;

    // Whether the configured account list contained an unparseable token. Carried separately from
    // the ids because a dropped token leaves no record of itself among them.
    bool protectedAccountsMalformed = false;

    // The operator supplied confirmation digest. Empty means preview only.
    std::string suppliedConfirmation;
};

struct RandomPlayerbotCleanupPlan
{
    RandomPlayerbotCleanupRefusal refusal = RandomPlayerbotCleanupRefusal::None;

    // Sorted and deduplicated, so the digest depends on the target and not on the order the rows
    // came back in.
    std::vector<uint32> accountIds;
    std::vector<uint32> characterGuids;

    // Lowercase hex over the exact target. Two runs that would delete the same thing produce the
    // same value, and any change to who is included changes it.
    std::string confirmationDigest;

    [[nodiscard]] bool MayMutate() const { return refusal == RandomPlayerbotCleanupRefusal::None; }
};

/*
 * Builds the plan. Always returns the complete computed target, including when it refuses, so the
 * caller can print an accurate preview of what a confirmed run would do.
 */
[[nodiscard]] RandomPlayerbotCleanupPlan RandomPlayerbotBuildCleanupPlan(RandomPlayerbotCleanupRequest const& request);

/*
 * Rebuilds the plan immediately before mutation and reports whether it still describes the same
 * target. A cleanup that passed its confirmation can still be overtaken by a character created
 * between the preview and the delete, and the captured identifiers would then be stale.
 */
[[nodiscard]] bool RandomPlayerbotCleanupPlansAgree(RandomPlayerbotCleanupPlan const& captured,
                                                    RandomPlayerbotCleanupPlan const& rebuilt);

#endif  // PLAYERBOTS_RANDOMPLAYERBOTCLEANUPPLAN_H
