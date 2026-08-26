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
* A digest authorizes only the exact cohort shown in the preview.
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
3. Restart the worldserver. The hook runs at startup from `RandomPlayerbotFactory::CreateRandomBots`,
   builds the cohort, prints the confirmation digest, rebuilds the cohort and compares, deletes, then
   calls `World::StopNow`. The server stopping is the expected outcome, not a failure.
4. Set `PlayerbotsLifecycle.CleanupRequested = 0`.
5. Restart the worldserver. `RandomPlayerbotFactory::CreateRandomBots` runs unfenced and recreates the
   population up to `AiPlayerbot.MinRandomBots`, at ten characters per account, faction balanced by
   `RandomPlayerbotFactionBalance`. Nothing else has to be run.

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
