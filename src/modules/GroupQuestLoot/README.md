# GroupQuestLoot

GroupQuestLoot is an optional core module that marks quest-relevant loot for
group sharing. It covers both database quest-only drops (`needs_quest`) and
valid items whose item prototype starts a quest (`StartQuest != 0` with a
matching quest template).

## Build

Enable the module when configuring the server:

```sh
cmake -S . -B build -DBUILD_GROUP_QUEST_LOOT=ON
```

The module is independent of PlayerBots and Eluna, and does not add or require
any SQL changes.

## Behavior

The module only selects which loot entries are eligible for group sharing. The
core still rolls each entry with its configured chance and count, evaluates its
conditions, and applies the existing `AllowedForPlayer` checks. Ordinary loot
is unaffected.

The existing 1.12 client/core limit of 16 visible loot slots still applies.
This module does not change loot-slot ordering or make additional generated
items visible after that limit is reached.
