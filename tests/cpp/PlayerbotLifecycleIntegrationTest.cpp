#include <string>
#include <utility>
#include <vector>

#include "Bot/Lifecycle/RandomPlayerbotCleanupPlan.h"
#include "gtest/gtest.h"

namespace
{
RandomPlayerbotCandidateAccount BotAccount(uint32 accountId, std::string name, std::vector<uint32> guids)
{
    RandomPlayerbotCandidateAccount account;
    account.accountId = accountId;
    account.accountName = std::move(name);
    account.hasRandomBotOwnershipRow = true;
    account.characterGuids = std::move(guids);
    for (uint32 guid : account.characterGuids)
        account.characterNames.emplace(guid, "Bot" + std::to_string(guid));
    return account;
}

RandomPlayerbotCleanupRequest CleanRequest()
{
    RandomPlayerbotCleanupRequest request;
    request.accountNamePrefix = "rndbot";
    request.generatedAccountCount = 3;
    request.candidates = {BotAccount(10, "RNDBOT0", {101, 102}), BotAccount(11, "RNDBOT1", {103}),
                          BotAccount(12, "RNDBOT2", {104, 105})};
    request.protectedAccounts = {{900, true, {9000}}};
    return request;
}
}  // namespace

TEST(PlayerbotLifecycleCleanupTest, ConfirmationUnlocksOnlyTheExactInspectedCohort)
{
    RandomPlayerbotCleanupRequest request = CleanRequest();
    RandomPlayerbotCleanupPlan const preview = RandomPlayerbotBuildCleanupPlan(request);
    ASSERT_EQ(preview.refusal, RandomPlayerbotCleanupRefusal::ConfirmationMissing);
    request.suppliedConfirmation = preview.confirmationDigest;

    RandomPlayerbotCleanupPlan const approved = RandomPlayerbotBuildCleanupPlan(request);
    EXPECT_TRUE(approved.MayMutate());

    request.candidates.front().characterNames.at(101) = "Renamed";
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal, RandomPlayerbotCleanupRefusal::ConfirmationMismatch);
}

TEST(PlayerbotLifecycleCleanupTest, ProtectedAccountsFailClosed)
{
    RandomPlayerbotCleanupRequest request = CleanRequest();

    // An account owning ANY character in the cohort is refused, not just one that was named.
    request.protectedAccounts = {{900, true, {103}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedAccountTargeted);

    // Guarding still fires on a second character of the same account, which is the whole point of
    // scoping protection to the account: a character added later is covered without a config edit.
    request.protectedAccounts = {{900, true, {9000, 104}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedAccountTargeted);

    // A mistyped id matches no account and fails closed.
    request.protectedAccounts = {{4242, false, {}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedAccountUnresolved);

    /*
     * A real account, not in the cohort, that owns no character. It guards nothing, and the database
     * layer cannot distinguish that from a character query that failed, so it is refused rather than
     * accepted as a well-formed guard over an empty set. Remove the id to proceed deliberately.
     */
    request.protectedAccounts = {{900, true, {}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal, RandomPlayerbotCleanupRefusal::ProtectedAccountEmpty);

    // One protected account owning nothing refuses even when another is a real guard, so a stale id
    // cannot ride along beside a good one.
    request.protectedAccounts = {{900, true, {9000}}, {901, true, {}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal, RandomPlayerbotCleanupRefusal::ProtectedAccountEmpty);

    /*
     * The same empty list, but the protected account IS a target. The character query returned
     * nothing, which the database layer reports identically for an empty account and for a failed
     * query, so the guid intersection sees nothing to refuse. The account id must refuse it anyway,
     * or a confirmed run deletes account 10 and every character it owns.
     */
    request.protectedAccounts = {{10, true, {}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedAccountTargeted);
    request.suppliedConfirmation = RandomPlayerbotBuildCleanupPlan(request).confirmationDigest;
    EXPECT_FALSE(RandomPlayerbotBuildCleanupPlan(request).MayMutate());

    // Configuring nothing at all is refused.
    request.protectedAccounts.clear();
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedAccountsUnconfigured);
}

TEST(PlayerbotLifecycleCleanupTest, MalformedProtectedAccountListRefusesEvenWithAValidEntry)
{
    RandomPlayerbotCleanupRequest request = CleanRequest();

    // The dangerous shape: one good id keeps the list nonempty, so neither Unconfigured nor
    // Unresolved fires, and the bad token vanishes without trace.
    request.protectedAccountsMalformed = true;
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedAccountsMalformed);

    // A digest taken while malformed must not authorise a later clean run, and vice versa.
    std::string const malformedDigest = RandomPlayerbotBuildCleanupPlan(request).confirmationDigest;
    request.protectedAccountsMalformed = false;
    EXPECT_NE(RandomPlayerbotBuildCleanupPlan(request).confirmationDigest, malformedDigest);
}

TEST(PlayerbotLifecycleCleanupTest, ProtectionDigestIsSelfDelimiting)
{
    RandomPlayerbotCleanupRequest one = CleanRequest();
    one.protectedAccounts = {{1, true, {2, 3}}};

    // Same numbers, different record boundaries. Without mixed counts both sets serialise to an
    // identical byte stream, so the digest cannot tell them apart.
    RandomPlayerbotCleanupRequest many = CleanRequest();
    many.protectedAccounts = {{1, true, {}}, {2, true, {}}, {3, true, {}}};

    EXPECT_NE(RandomPlayerbotBuildCleanupPlan(one).confirmationDigest,
              RandomPlayerbotBuildCleanupPlan(many).confirmationDigest);
}

TEST(PlayerbotLifecycleCleanupTest, AccountNameAndOwnershipMustAgree)
{
    RandomPlayerbotCleanupRequest request = CleanRequest();
    request.candidates.front().hasRandomBotOwnershipRow = false;
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::AccountNameOwnershipMismatch);

    request = CleanRequest();
    request.candidates.push_back(BotAccount(99, "HUMAN", {999}));
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::AccountNameOwnershipMismatch);
}
