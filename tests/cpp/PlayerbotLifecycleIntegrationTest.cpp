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
    request.protectedCharacters = {{"Keeper", {900}}};
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

TEST(PlayerbotLifecycleCleanupTest, ProtectedCharactersFailClosed)
{
    RandomPlayerbotCleanupRequest request = CleanRequest();
    request.protectedCharacters = {{"Keeper", {103}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedCharacterTargeted);

    request.protectedCharacters = {{"Misspelled", {}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedCharacterUnresolved);

    request.protectedCharacters = {{"Duplicate", {900, 901}}};
    EXPECT_EQ(RandomPlayerbotBuildCleanupPlan(request).refusal,
              RandomPlayerbotCleanupRefusal::ProtectedCharacterAmbiguous);
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
