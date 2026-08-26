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
    request.protectedAccounts = {{900, true, {9000}}};
    return request;
}
}  // namespace

int main()
{
    sConfigMgr->SetOption<bool>("PlayerbotsLifecycle.CleanupRequested", true);
    sConfigMgr->SetOption<std::string>("PlayerbotsLifecycle.CleanupConfirmation", "confirmed-digest");
    sConfigMgr->SetOption<std::string>("PlayerbotsLifecycle.ProtectedAccounts", " 157, 158 ,,159 ");
    ReloadPlayerbotLifecycleConfig();
    Require(sPlayerbotLifecycleConfig.cleanupRequested);
    Require(sPlayerbotLifecycleConfig.cleanupConfirmation == "confirmed-digest");
    // An empty token from a doubled comma is formatting, not a mistake, so this list is clean.
    Require(sPlayerbotLifecycleConfig.protectedAccounts == (std::vector<std::uint32_t>{157, 158, 159}));
    Require(!sPlayerbotLifecycleConfig.protectedAccountsMalformed);

    /*
     * Every token that is not a valid nonzero id must RECORD itself, not merely disappear. The
     * dangerous case is the mixed one: a surviving valid id keeps the list nonempty, so without the
     * flag nothing refuses and the operator gets a digest for a smaller guard set than they wrote.
     */
    for (char const* bad : {"157,158abc", "157,0", "157,-1", "157,4294967296", "bogus"})
    {
        sConfigMgr->SetOption<std::string>("PlayerbotsLifecycle.ProtectedAccounts", bad);
        ReloadPlayerbotLifecycleConfig();
        Require(sPlayerbotLifecycleConfig.protectedAccountsMalformed);
    }

    sConfigMgr->SetOption<std::string>("PlayerbotsLifecycle.ProtectedAccounts", " 157, 158 ,,159 ");
    ReloadPlayerbotLifecycleConfig();

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

    /*
     * The hole this guards: a protected account that IS a deletion target, whose character query
     * came back empty. The database layer reports an empty account and a failed query identically,
     * so the guid intersection has nothing to match and would pass the plan.
     */
    RandomPlayerbotCleanupRequest targetedEmpty = CleanRequest();
    targetedEmpty.protectedAccounts = {{10, true, {}}};
    Require(RandomPlayerbotBuildCleanupPlan(targetedEmpty).refusal ==
            RandomPlayerbotCleanupRefusal::ProtectedAccountTargeted);
    targetedEmpty.suppliedConfirmation = RandomPlayerbotBuildCleanupPlan(targetedEmpty).confirmationDigest;
    Require(!RandomPlayerbotBuildCleanupPlan(targetedEmpty).MayMutate());

    // Same numbers, different record boundaries: the digest must distinguish them.
    RandomPlayerbotCleanupRequest oneAccount = CleanRequest();
    oneAccount.protectedAccounts = {{1, true, {2, 3}}};
    RandomPlayerbotCleanupRequest manyAccounts = CleanRequest();
    manyAccounts.protectedAccounts = {{1, true, {}}, {2, true, {}}, {3, true, {}}};
    Require(RandomPlayerbotBuildCleanupPlan(oneAccount).confirmationDigest !=
            RandomPlayerbotBuildCleanupPlan(manyAccounts).confirmationDigest);

    // A malformed list refuses even though a valid id keeps it nonempty.
    RandomPlayerbotCleanupRequest malformed = CleanRequest();
    malformed.protectedAccountsMalformed = true;
    Require(RandomPlayerbotBuildCleanupPlan(malformed).refusal ==
            RandomPlayerbotCleanupRefusal::ProtectedAccountsMalformed);

    RandomPlayerbotCleanupRequest unprotected = CleanRequest();
    unprotected.protectedAccounts.clear();
    unprotected.suppliedConfirmation = RandomPlayerbotBuildCleanupPlan(unprotected).confirmationDigest;
    Require(RandomPlayerbotBuildCleanupPlan(unprotected).refusal ==
            RandomPlayerbotCleanupRefusal::ProtectedAccountsUnconfigured);
    return EXIT_SUCCESS;
}
