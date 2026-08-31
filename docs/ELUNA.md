# Eluna integration

This branch integrates [Eluna](https://github.com/ElunaLuaEngine/Eluna) into
the Turtle WoW MaNGOS core as the `src/modules/Eluna` Git submodule. The
submodule is pinned to commit `1b06f28ff3a00054d915d824c725fb4283fee74d`, the
revision shared by the maintained VMaNGOS and cMaNGOS integrations when this
port was prepared.

This repository remains a custom Turtle WoW MaNGOS core; it is not VMaNGOS.
The host selects Eluna's VMaNGOS compatibility backend (`ELUNA_VMANGOS`) and
the vanilla client expansion (`ELUNA_EXPANSION=0`) because that backend most
closely matches this fork's classic API surface. Eluna's `ELUNA_MANGOS`
backend targets a different MaNGOS API generation and expects headers and
revision definitions this tree does not provide.

The backend macro normally makes Eluna report the core name as `vMaNGOS`.
Turtle's custom-method layer overrides `GetCoreName()` to return `MaNGOS` and
`GetCoreVersion()` to return this repository's revision hash. The backend is
an implementation detail, not the server's identity.

## Checkout

Clone recursively, or initialize Eluna after cloning:

```sh
git submodule update --init --recursive src/modules/Eluna
```

CMake stops with an actionable error when Eluna is enabled but the submodule
has not been initialized.

## Configure and build

Eluna and its Lua 5.2 runtime are enabled by default:

```sh
cmake -S . -B build -DBUILD_ELUNA=ON -DELUNA_LUA_VERSION=lua52
cmake --build build --config Release --target mangosd
ctest --test-dir build -C Release --output-on-failure
```

`ELUNA_LUA_VERSION` accepts `lua51`, `lua52`, `lua53`, or `lua54`. Lua source
archives are downloaded from lua.org with pinned SHA-256 checksums and linked
statically. `BUILD_ELUNA_TESTS=ON` builds a small runtime linkage test. Use
`BUILD_ELUNA=OFF` for a core-only regression build.

`BUILD_ELUNA` is the compile-time switch; `Eluna.Enabled` is the independent
runtime switch. A build that contains Eluna can therefore run with Lua disabled
without needing a second binary.

Windows still requires the normal ACE development dependency. For example,
pass its prefix as `-DACE_ROOT=C:/path/to/ace` when CMake cannot discover it.

## Runtime configuration

The distributed `mangosd.conf` enables Eluna and reads scripts from
`./lua_scripts`, relative to the server working directory. The relevant keys
are:

- `Eluna.Enabled`
- `Eluna.TraceBack`
- `Eluna.ReloadCommand`
- `Eluna.UseUnsafeMethods`
- `Eluna.UseDeprecatedMethods`
- `Eluna.OnlyOnMaps`
- `Eluna.ScriptPath`
- `Eluna.RequirePaths`
- `Eluna.RequireCPaths`
- `Eluna.ReloadSecurityLevel`
- `ElunaErrorLogFile`

Place server scripts under `lua_scripts`. Eluna's bundled extensions are
installed into that directory by the CMake install target. With reloads
enabled, an authorized account can use `.reload eluna`.

## Turtle-specific architecture

Eluna owns one global Lua state and optional per-map states. World and map
lifecycle calls are wired directly into the core so state creation, updates,
and teardown stay ordered. Other events are bridged through Turtle's existing
`ScriptObject` registries where possible, preserving the existing C++ script
precedence and cancellation behavior. Hooks not represented by that registry
are placed at their concrete gameplay lifecycle points.

Lua state access is single-threaded. When Eluna is enabled, a map configured
for parallel motion, object, or visibility updates is forced back to the
single-threaded path. This is intentional: invoking one Lua state concurrently
would corrupt its stack and event queues.

The integration includes:

- global/map state lifecycle, configuration reload, startup, shutdown, and
  timed updates;
- creature Lua AI fallback and instance data fallback after native C++ scripts;
- player, chat/command, packet, loot, trade, mail, group, guild, auction,
  battleground, weather, and game-event dispatch;
- creature, gameobject, item, gossip, quest, area-trigger, summon, and dummy
  spell-effect dispatch;
- Eluna object event processors and the compatibility-backend method bindings
  required by the pinned engine revision; and
- a host-side `SpellInfo` compatibility layer backed by Turtle's canonical
  `spell_template` records.

## Verification checklist

Before merging an Eluna update:

1. Confirm `git submodule status src/modules/Eluna` reports the expected commit.
2. Configure and build `mangosd` with `BUILD_ELUNA=ON`.
3. Run `ctest`; `eluna.lua-runtime` must pass.
4. Configure and build `mangosd` with `BUILD_ELUNA=OFF`.
5. Start a configured development realm, confirm the Eluna startup banner and
   no errors in `ElunaErrors.log`, load a small login hook, and exercise
   `.reload eluna`.

Realm databases, client data, credentials, generated configuration, and
operator-specific integration scripts remain outside version control.

## Updating the submodule

Treat an Eluna revision change as an API update, not a routine dependency bump.
Move the submodule deliberately, record the new commit in the parent repository,
then repeat the complete verification checklist above. In particular, review
the selected compatibility backend for changed method signatures or newly
registered userdata before adapting Turtle-specific core types.

No file inside the Eluna submodule is patched or renamed. Compatibility headers
and custom methods live in the host tree. The MSVC parameter-name workaround is
applied to a generated build-directory copy, and becomes a no-op if upstream
removes that spelling. The SpellInfo fallback checks the registered Lua API at
startup, so a future native upstream implementation takes precedence instead of
being registered twice.

```sh
git -C src/modules/Eluna fetch origin
git -C src/modules/Eluna checkout <reviewed-commit>
git add src/modules/Eluna
```

## SpellInfo compatibility adaptation

Eluna already defines an `ElunaSpellInfo` value wrapper for non-Trinity cores,
but upstream only registers the wrapper and its Lua methods for Trinity-family
builds. The custom-method extension in `src/game/Eluna/CustomMethods.h`
registers it for this Turtle WoW MaNGOS port without modifying the pinned
submodule. It supplies `Aura:GetSpellInfo`, `Spell:GetSpellInfo`, the global
`GetSpellInfo(spellId)`, and the stable vanilla-compatible portion of the
documented SpellInfo API.

The returned object references the immutable `SpellEntry` owned by
`SpellMgr`. In this fork those entries are populated from the world
database's `spell_template` table, so Lua sees Turtle's custom spell rows and
SQL-adjusted values rather than an unrelated stock DBC snapshot. Indexed
effect access is restricted to vanilla's three effect slots, with a Lua
argument error for an out-of-range index.

Methods that depend on post-vanilla fields or Trinity-only systems are not
registered. Examples include extended attribute words 5-7, rune costs,
`AuraEffect`, 96-bit effect class masks, and Trinity's structured target and
immunity helpers. This is an intentional capability boundary rather than
returning fabricated values.
