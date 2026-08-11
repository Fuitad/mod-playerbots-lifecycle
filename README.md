> **Work in progress**

This project is not ready for installation or use. It provides no deployment or compatibility guarantee.

# Playerbots Lifecycle

Playerbots Lifecycle is an AzerothCore module that owns safe random bot account cleanup. It captures the exact
account and character cohort, prints a confirmation digest, protects named characters, asks every installed
Playerbots extension to prepare for the purge, and then deletes only the confirmed accounts.

The module leaves random bot creation and standard Wrath gameplay in mod-playerbots. Cleanup intent, confirmation,
and protected character settings belong to this module and are documented in
`conf/mod_playerbots_lifecycle.conf.dist`.

## Safety contract

* An account is selected only when its generated name and its random bot ownership row agree.
* An empty protected character list refuses the cleanup.
* Every protected name must resolve to exactly one character outside the selected cohort.
* A digest authorizes only the exact cohort shown in the preview.
* The cohort is rebuilt immediately before any deletion.
* Every extension preparation must succeed before any account is deleted.

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
