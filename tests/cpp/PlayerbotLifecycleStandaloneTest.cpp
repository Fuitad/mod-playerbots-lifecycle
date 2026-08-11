#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "Bot/Lifecycle/PlayerbotLifecycleConfig.h"
#include "Bot/Lifecycle/RandomPlayerbotCleanupPlan.h"
#include "Config.h"

namespace
{
void Require(bool condition)
{
    if (!condition)
        std::exit(EXIT_FAILURE);
}

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
    request.generatedAccountCount = 2;
    request.candidates = {BotAccount(10, "RNDBOT0", {100, 101}), BotAccount(11, "RNDBOT1", {102})};
    request.protectedCharacters = {{"Keeper", {900}}};
    return request;
}
}  // namespace

int main()
{
    sConfigMgr->SetOption<bool>("PlayerbotsLifecycle.CleanupRequested", true);
    sConfigMgr->SetOption<std::string>("PlayerbotsLifecycle.CleanupConfirmation", "confirmed-digest");
    sConfigMgr->SetOption<std::string>("PlayerbotsLifecycle.ProtectedCharacters", " Keeper, Second Keeper ");
    ReloadPlayerbotLifecycleConfig();
    Require(sPlayerbotLifecycleConfig.cleanupRequested);
    Require(sPlayerbotLifecycleConfig.cleanupConfirmation == "confirmed-digest");
    Require(sPlayerbotLifecycleConfig.protectedCharacters == (std::vector<std::string>{"Keeper", "Second Keeper"}));

    RandomPlayerbotCleanupRequest request = CleanRequest();
    RandomPlayerbotCleanupPlan const preview = RandomPlayerbotBuildCleanupPlan(request);
    Require(preview.refusal == RandomPlayerbotCleanupRefusal::ConfirmationMissing);
    Require(preview.accountIds == (std::vector<uint32>{10, 11}));
    Require(preview.characterGuids == (std::vector<uint32>{100, 101, 102}));
    Require(!preview.confirmationDigest.empty());

    request.suppliedConfirmation = preview.confirmationDigest;
    RandomPlayerbotCleanupPlan const approved = RandomPlayerbotBuildCleanupPlan(request);
    Require(approved.MayMutate());
    Require(RandomPlayerbotCleanupPlansAgree(approved, RandomPlayerbotBuildCleanupPlan(request)));

    request.candidates.front().characterGuids.push_back(103);
    request.candidates.front().characterNames.emplace(103, "Bot103");
    Require(!RandomPlayerbotCleanupPlansAgree(approved, RandomPlayerbotBuildCleanupPlan(request)));

    RandomPlayerbotCleanupRequest stranger = CleanRequest();
    stranger.candidates.push_back({99, "RNDBOTFAN", false, {999}, {{999, "Human"}}});
    Require(RandomPlayerbotBuildCleanupPlan(stranger).accountIds == (std::vector<uint32>{10, 11}));

    RandomPlayerbotCleanupRequest mismatch = CleanRequest();
    mismatch.candidates.front().hasRandomBotOwnershipRow = false;
    Require(RandomPlayerbotBuildCleanupPlan(mismatch).refusal ==
            RandomPlayerbotCleanupRefusal::AccountNameOwnershipMismatch);

    RandomPlayerbotCleanupRequest unprotected = CleanRequest();
    unprotected.protectedCharacters.clear();
    unprotected.suppliedConfirmation = RandomPlayerbotBuildCleanupPlan(unprotected).confirmationDigest;
    Require(RandomPlayerbotBuildCleanupPlan(unprotected).refusal ==
            RandomPlayerbotCleanupRefusal::ProtectedCharactersUnconfigured);
    return EXIT_SUCCESS;
}
