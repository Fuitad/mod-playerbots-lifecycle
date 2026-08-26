> **Work in progress**

This project is not ready for installation or use. It provides no deployment or compatibility guarantee.

# Playerbots Lifecycle

Playerbots Lifecycle is an AzerothCore module that owns safe random bot account cleanup. It captures the exact
account and character cohort, prints a confirmation digest, protects every character owned by a configured
account, asks every installed
Playerbots extension to prepare for the purge, and then deletes only the confirmed accounts.

The module leaves random bot creation and standard Wrath gameplay in mod-playerbots. Cleanup intent, confirmation,
and protected account settings belong to this module and are documented in
`conf/mod_playerbots_lifecycle.conf.dist`.

## Safety contract

* An account is selected only when its generated name and its random bot ownership row agree.
* An empty protected account list refuses the cleanup.
* Every protected account id must match an existing account.
* A protected account that owns no character refuses the cleanup. It guards nothing, and the database
  layer reports an empty account and a failed character query identically, so neither reading is
  allowed to pass as a valid guard.
* No character owned by a protected account may fall inside the selected cohort. Protection is
  account scoped, so characters created after the list was written are covered too.
* A digest authorizes only the exact cohort shown in the preview, with one known limitation: the
  database layer reports a failed character query and an account with no characters identically, so a
  transient query failure can produce a plan whose digest omits characters that `AccountMgr::DeleteAccount`
  later re-reads and deletes. The plan carries no snapshot-completeness flag, so rebuilding and comparing
  does not close it. Treat the digest as authorization for the cohort the preview printed, and audit the
  database afterwards rather than assuming the two agreed.
* The cohort is rebuilt immediately before any deletion.
* Every extension preparation must succeed before any account is deleted.

## Running a population wipe

This module is the in-server wipe. Searching for "wipe", "reset", or "recreate" finds nothing anywhere in
the tree, and the Python tooling named `population_*` lives in a different module
(`mod-playerbots-economy/tools/`), so the two are easy to confuse. They are not alternatives:

| Job | Where it lives |
|---|---|
| Delete the random bot cohort from a running server | This module, via `PlayerbotsLifecycle.CleanupRequested` |
| Audit, back up, or plan a population change offline | `mod-playerbots-economy/tools/population_*.py` |
| Recreate the cohort afterwards | Nothing. `mod-playerbots` does it automatically |

### The cycle

1. Set `PlayerbotsLifecycle.ProtectedAccounts` to the comma separated **account ids** that must survive
   (the live realm's protected account is `157`). Protection is account scoped on purpose, so every
   character the account owns is covered, including ones created after the list was written. An empty or
   malformed list refuses the cleanup, and every id must match an existing account.

   This setting used to be `PlayerbotsLifecycle.ProtectedCharacters`, taking character names. A deployed
   `.conf` still carrying the old key leaves the new one empty, which refuses the cleanup rather than
   running it unprotected. Update the key before requesting a wipe.
2. Set `PlayerbotsLifecycle.CleanupRequested = 1`.
3. Run the worldserver ONCE, with its process supervisor out of the way (see the warning below). The
   hook runs at startup from `RandomPlayerbotFactory::CreateRandomBots`, builds the cohort, prints the
   confirmation digest, rebuilds the cohort and compares, deletes, then calls `World::StopNow`. The
   process exiting is the expected outcome, not a failure.

   The first run leaves `PlayerbotsLifecycle.CleanupConfirmation` empty on purpose. That refuses with
   `confirmation_missing`, deletes nothing, and prints the cohort and its digest so it can be reviewed.
   Copy the digest into `CleanupConfirmation` and run again to actually delete.

   **The process exiting is not proof that anything worked.** `SHUTDOWN_EXIT_CODE` is zero, so the run
   exits zero on a preview refusal, on a changed target, on a successful deletion, and on a
   `DeleteCleanupPlan` failure alike. Worse, that function deletes the Playerbots rows and then the
   accounts one at a time, so a failure partway leaves earlier accounts already gone and skips
   `OnBotPurge`, which is what removes the auction rows. Before resetting anything, require this exact
   line:

   ```text
   Random bot accounts and owned data were deleted.
   ```

   It is emitted only when the deletion actually succeeded. Then audit the database: no `characters`
   rows on the generated accounts, no `playerbots_account_type` rows for them, and no `auctionhouse`
   rows owned by the deleted guids.
4. Set `PlayerbotsLifecycle.CleanupRequested = 0`, and clear `CleanupConfirmation`.
5. Restart the worldserver. `RandomPlayerbotFactory::CreateRandomBots` runs unfenced and recreates the
   population, faction balanced by `RandomPlayerbotFactionBalance`. Nothing else has to be run.

   The stored cohort is sized from `AiPlayerbot.MaxRandomBots`, NOT `MinRandomBots`.
   `CalculateTotalAccountCount` divides it by the characters available per account, which is 9 rather
   than 10 whenever `DisableDeathKnightLogin` is set or the realm is not on Wrath, and is reduced
   further when the faction ratios are uneven. Every generated account is then filled to ten stored
   characters regardless. On this realm, `MaxRandomBots = 200` with death knight login disabled and
   50/50 ratios gives `ceil(200 / 9) = 23` accounts and 230 stored characters. `MinRandomBots` and
   `MaxRandomBots` bound the number of bots ONLINE at runtime; they do not size the stored pool.

   Ports listening does not prove the population came back. A missing name pool makes
   `CreateRandomBots` return early, and an individual character creation failure logs and continues, so
   the realm can serve all three ports with an empty or partial cohort. Require both completion markers
   and then count the rows:

   ```text
   >> 23 random bot accounts with 230 characters available
   Account type assignment complete: 23 RNDbot accounts, 0 AddClass accounts, 0 unassigned
   ```

### The supervisor will fight you, and it wins

A cleanup run ends by calling `World::StopNow`, and the process exits. `CleanupRequested` is startup
configuration, so a supervisor that restarts the process runs the cleanup again, and again. That is an
unattended loop.

On Pierre's macOS host, `com.azeroth.worldserver.plist` sets `KeepAlive` to an unconditional `true`,
which restarts the job **regardless of exit status**. The zero exit is not what triggers it; a crash
would be restarted too. Note that a `KeepAlive` dictionary with `SuccessfulExit = true` would still loop,
because cleanup exits zero; only `SuccessfulExit = false` would leave it down after a clean exit.
launchd throttles respawns to roughly ten seconds, so this is a slow relentless loop rather than a hot
one. Take the service out of the picture for the run rather than restarting it:

```bash
launchctl bootout "gui/$(id -u)/com.azeroth.worldserver"
# poll until `launchctl print` no longer finds it, then run the binary the same way the service does:
source /Users/pierre/azeroth-server/etc/secrets.env
cd /Users/pierre/azeroth-server/bin && ./worldserver -c /Users/pierre/azeroth-server/etc/worldserver.conf
# it prints the plan and exits on its own; then reset the config and bring the service back:
launchctl bootstrap "gui/$(id -u)" /Users/pierre/azeroth-server/launchd/com.azeroth.worldserver.plist
```

A preview run is quick because it refuses before reaching any deletion. It is deletion-safe, not read
only: `GatherCleanupRequest` calls `CalculateTotalAccountCount` before the confirmation is checked, and
that function issues `UPDATE playerbots_account_type` statements when `MaxRandomBots` or
`AddClassAccountPoolSize` is zero. Budget for a normal worldserver startup either way, check that
`launchctl bootstrap` actually succeeded, and confirm ports 8085, 8888 and 24601 are listening again
before walking away.

Step 5 is the part that surprises people. There is no recreation script to find because there is no
recreation script to write. The factory is idempotent: it skips accounts that already exist and accounts
that already hold ten characters, so a partial cohort is completed rather than duplicated.

### What recreation does not preserve

The factory produces a fresh random cohort, not the previous one. Names, appearance, and personality
affinities are redrawn. `PlayerbotPersonalityMgr::Generate` uses unseeded `urand()` for every affinity
except the economy one, so a population cannot be reproduced by rerunning the factory.

A deterministic cohort builder was written on 2026-08-14 and never merged. It survives as the tag
`archive/option-b-recreation` in the `mod-playerbots-economy` repository (chain `f96e03d..d464f66`,
around 4300 lines, including `src/Bot/Population/` and `tools/population_option_b*.py`). Its config keys
(`PlayerbotsEconomy.FrozenPopulationEnabled`, `PopulationOperationMode`, `PopulationOperationCohort`,
`PopulationOperationGuard`) may still be present in deployed `.conf` files. **They are dead.** No module
reads them and the strings are absent from the built worldserver. Do not treat their presence as
evidence that the frozen population system is active.

### What the deletion covers

Deletion runs `AccountMgr::DeleteAccount`, which calls `Player::DeleteFromDB` per character. That covers
mail and mail items, `item_instance` owned by the character, guild membership (`Guild::DeleteMember`),
guild event logs, pets, and the rest of the per character tables.

It does **not** cover `auctionhouse`. No core path does. `mod-playerbots-economy` closes that gap through
the extension hooks: it refuses the purge in `PrepareBotPurge` when a character outside the cohort holds
a live bid on a doomed listing, and deletes the auction rows in `OnBotPurge` once the accounts are gone.
Without a module implementing those hooks, a wipe leaves one orphaned `auctionhouse` row per listing
pointing at an item row that was already deleted.

If you add a module that owns rows keyed on a character guid, implement `OnBotPurge` for it. The registry
runs every extension's `PrepareBotPurge` without short-circuiting, so `PrepareBotPurge` is for validation
and reversible intent only. Anything destructive belongs in `OnBotPurge`.

## Dependencies

* A Playerbot compatible AzerothCore checkout
* The public mod-playerbots fork with the generic extension registry

## Standalone verification

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

## License

Playerbots Lifecycle is licensed under the GNU General Public License version 2. See `LICENSE`.
