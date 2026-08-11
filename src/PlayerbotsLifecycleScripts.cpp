#include <algorithm>
#include <chrono>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "AccountMgr.h"
#include "Bot/Extension/PlayerbotExtension.h"
#include "Bot/Factory/RandomPlayerbotFactory.h"
#include "Bot/Lifecycle/PlayerbotLifecycleConfig.h"
#include "Bot/Lifecycle/RandomPlayerbotCleanupPlan.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "PlayerbotAIConfig.h"
#include "ScriptMgr.h"
#include "World.h"

namespace
{
RandomPlayerbotCleanupRequest GatherCleanupRequest()
{
    RandomPlayerbotCleanupRequest request;
    request.accountNamePrefix = sPlayerbotAIConfig.randomBotAccountPrefix;
    request.generatedAccountCount = RandomPlayerbotFactory::CalculateTotalAccountCount();
    request.suppliedConfirmation = sPlayerbotLifecycleConfig.cleanupConfirmation;

    std::map<uint32, RandomPlayerbotCandidateAccount> candidates;
    for (uint32 accountNumber = 0; accountNumber < request.generatedAccountCount; ++accountNumber)
    {
        std::string const generatedName = request.accountNamePrefix + std::to_string(accountNumber);
        uint32 const accountId = AccountMgr::GetId(generatedName);
        if (!accountId)
            continue;

        RandomPlayerbotCandidateAccount& candidate = candidates[accountId];
        candidate.accountId = accountId;
        if (!AccountMgr::GetName(accountId, candidate.accountName))
            candidate.accountName = generatedName;
    }

    if (QueryResult owned = PlayerbotsDatabase.Query("SELECT account_id FROM playerbots_account_type"))
    {
        do
        {
            uint32 const accountId = owned->Fetch()[0].Get<uint32>();
            RandomPlayerbotCandidateAccount& candidate = candidates[accountId];
            candidate.accountId = accountId;
            candidate.hasRandomBotOwnershipRow = true;
        } while (owned->NextRow());
    }

    for (auto& [accountId, candidate] : candidates)
    {
        if (candidate.accountName.empty())
            AccountMgr::GetName(accountId, candidate.accountName);

        if (QueryResult characters =
                CharacterDatabase.Query("SELECT guid, name FROM characters WHERE account = {}", accountId))
        {
            do
            {
                Field* fields = characters->Fetch();
                uint32 const guid = fields[0].Get<uint32>();
                candidate.characterGuids.push_back(guid);
                candidate.characterNames.emplace(guid, fields[1].Get<std::string>());
            } while (characters->NextRow());
        }

        request.candidates.push_back(candidate);
    }

    for (std::string const& configuredName : sPlayerbotLifecycleConfig.protectedCharacters)
    {
        RandomPlayerbotProtectedCharacter protectedCharacter;
        protectedCharacter.configuredName = configuredName;

        std::string escapedName = configuredName;
        CharacterDatabase.EscapeString(escapedName);
        if (QueryResult found = CharacterDatabase.Query("SELECT guid FROM characters WHERE name = '{}'", escapedName))
        {
            do
            {
                protectedCharacter.resolvedGuids.push_back(found->Fetch()[0].Get<uint32>());
            } while (found->NextRow());
        }
        request.protectedCharacters.push_back(std::move(protectedCharacter));
    }
    return request;
}

void LogCleanupPlan(RandomPlayerbotCleanupPlan const& plan, RandomPlayerbotCleanupRequest const& request)
{
    LOG_INFO("playerbots.lifecycle", "Random bot cleanup target: {} accounts, {} characters.", plan.accountIds.size(),
             plan.characterGuids.size());
    for (RandomPlayerbotCandidateAccount const& account : request.candidates)
    {
        bool const targeted = std::binary_search(plan.accountIds.begin(), plan.accountIds.end(), account.accountId);
        LOG_INFO("playerbots.lifecycle", "  account {} name={} ownership_row={} target={}", account.accountId,
                 account.accountName, account.hasRandomBotOwnershipRow, targeted);
        for (uint32 guid : account.characterGuids)
        {
            auto const name = account.characterNames.find(guid);
            LOG_INFO("playerbots.lifecycle", "    character {} name={} target={}", guid,
                     name == account.characterNames.end() ? "<missing>" : name->second, targeted);
        }
    }

    for (RandomPlayerbotProtectedCharacter const& protectedCharacter : request.protectedCharacters)
    {
        if (protectedCharacter.resolvedGuids.empty())
        {
            LOG_INFO("playerbots.lifecycle", "  protected name={} resolved=<none>", protectedCharacter.configuredName);
            continue;
        }
        for (uint32 guid : protectedCharacter.resolvedGuids)
            LOG_INFO("playerbots.lifecycle", "  protected name={} resolved={}", protectedCharacter.configuredName,
                     guid);
    }

    LOG_INFO("playerbots.lifecycle", "Confirmation digest: {}", plan.confirmationDigest);
    if (!plan.MayMutate())
    {
        LOG_ERROR("playerbots.lifecycle", "Refusing to delete anything: {}",
                  RandomPlayerbotCleanupRefusalName(plan.refusal));
        if (plan.refusal == RandomPlayerbotCleanupRefusal::ConfirmationMissing)
        {
            LOG_INFO("playerbots.lifecycle",
                     "Nothing was deleted. Set PlayerbotsLifecycle.CleanupConfirmation to the digest above and "
                     "restart. The digest stops matching if the target changes.");
        }
    }
}

std::string JoinIds(std::vector<uint32> const& ids)
{
    std::ostringstream joined;
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        if (index)
            joined << ',';
        joined << ids[index];
    }
    return joined.str();
}

bool CountIsZero(QueryResult const& result) { return result && result->Fetch()[0].Get<uint64>() == 0; }

bool CleanupPersisted(RandomPlayerbotCleanupPlan const& plan)
{
    std::string const accountIds = JoinIds(plan.accountIds);
    std::string const characterGuids = JoinIds(plan.characterGuids);
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    do
    {
        bool accountsGone = accountIds.empty() || CountIsZero(LoginDatabase.Query(
                                                      "SELECT COUNT(*) FROM account WHERE id IN ({})", accountIds));
        bool charactersGone =
            characterGuids.empty() ||
            CountIsZero(CharacterDatabase.Query("SELECT COUNT(*) FROM characters WHERE guid IN ({})", characterGuids));
        bool botRowsGone = characterGuids.empty() ||
                           CountIsZero(PlayerbotsDatabase.Query(
                               "SELECT COUNT(*) FROM playerbots_random_bots WHERE bot IN ({})", characterGuids));
        bool ownershipGone = accountIds.empty() ||
                             CountIsZero(PlayerbotsDatabase.Query(
                                 "SELECT COUNT(*) FROM playerbots_account_type WHERE account_id IN ({})", accountIds));
        if (accountsGone && charactersGone && botRowsGone && ownershipGone)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool DeleteCleanupPlan(RandomPlayerbotCleanupPlan const& plan)
{
    if (!GetPlayerbotExtensionRegistry().PrepareBotPurge(plan.characterGuids))
    {
        LOG_ERROR("playerbots.lifecycle",
                  "A Playerbots extension could not durably prepare the bot purge. "
                  "Nothing was deleted.");
        return false;
    }

    for (uint32 guid : plan.characterGuids)
    {
        PlayerbotsDatabase.DirectExecute("DELETE FROM playerbots_random_bots WHERE bot = {}", guid);
        PlayerbotsDatabase.DirectExecute("DELETE FROM playerbots_db_store WHERE guid = {}", guid);
        PlayerbotsDatabase.DirectExecute("DELETE FROM playerbots_guild_tasks WHERE owner = {}", guid);
    }
    for (uint32 accountId : plan.accountIds)
        PlayerbotsDatabase.DirectExecute("DELETE FROM playerbots_account_type WHERE account_id = {}", accountId);

    for (uint32 accountId : plan.accountIds)
    {
        if (AccountMgr::DeleteAccount(accountId) != AOR_OK)
        {
            LOG_ERROR("playerbots.lifecycle", "Account {} could not be deleted. The server will stop for inspection.",
                      accountId);
            return false;
        }
    }

    GetPlayerbotExtensionRegistry().OnBotPurge(plan.characterGuids);
    if (!CleanupPersisted(plan))
    {
        LOG_ERROR("playerbots.lifecycle",
                  "The cleanup did not become durable within 60 seconds. "
                  "The server will stop for inspection.");
        return false;
    }
    return true;
}

class PlayerbotsLifecycleExtension final : public PlayerbotExtension
{
public:
    bool HandleRandomBotAccountCleanup() override
    {
        if (!sPlayerbotLifecycleConfig.cleanupRequested)
            return false;

        RandomPlayerbotCleanupRequest const request = GatherCleanupRequest();
        RandomPlayerbotCleanupPlan const plan = RandomPlayerbotBuildCleanupPlan(request);
        LogCleanupPlan(plan, request);
        if (!plan.MayMutate())
        {
            World::StopNow(SHUTDOWN_EXIT_CODE);
            return true;
        }

        RandomPlayerbotCleanupPlan const rebuilt = RandomPlayerbotBuildCleanupPlan(GatherCleanupRequest());
        if (!RandomPlayerbotCleanupPlansAgree(plan, rebuilt))
        {
            LOG_ERROR("playerbots.lifecycle", "The cleanup target changed after confirmation. Nothing was deleted.");
            World::StopNow(SHUTDOWN_EXIT_CODE);
            return true;
        }

        if (DeleteCleanupPlan(plan))
        {
            LOG_INFO("playerbots.lifecycle", "Random bot accounts and owned data were deleted.");
            LOG_INFO("playerbots.lifecycle", "Reset PlayerbotsLifecycle.CleanupRequested to 0 before restarting.");
        }
        World::StopNow(SHUTDOWN_EXIT_CODE);
        return true;
    }
};

class PlayerbotsLifecycleWorldScript final : public WorldScript
{
public:
    PlayerbotsLifecycleWorldScript() : WorldScript("PlayerbotsLifecycleWorldScript") {}

    void OnAfterConfigLoad(bool) override { ReloadPlayerbotLifecycleConfig(); }
};
}  // namespace

void AddPlayerbotsLifecycleScripts()
{
    static PlayerbotsLifecycleExtension extension;
    GetPlayerbotExtensionRegistry().Register(extension);
    new PlayerbotsLifecycleWorldScript();
}
