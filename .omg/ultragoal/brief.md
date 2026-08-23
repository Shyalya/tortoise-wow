# Ultragoal Brief: Sync Upstream Commits, Database Migration Check, and Release Build

## Objective
Pull and merge the latest 6 commits from `origin/playerbots-integration-gh`, verify if any database migrations or schema updates are pending against MariaDB (`tw_world`, `tw_char`), execute a full/incremental CMake Release build with all required flags (`-DBUILD_PLAYERBOTS=ON -DUSE_EXTRACTORS=ON -DALLOW_TURTLE_ADDONS=ON`), and verify server service startup and health.

## Micro-Goals
1. **goal-1: Service Halt, Stash & Pull/Merge**: Stop systemd units `turtle-mangosd` and `turtle-realmd`, stash local uncommitted modifications, pull fast-forward commits from `origin/playerbots-integration-gh`, pop stashed changes cleanly.
2. **goal-2: Database Migration Check & Apply**: Inspect for any new SQL updates or schema migrations in `sql/database_updates/` and `sql/base/`, check `tw_world.migrations` and `character_inventory_copy` table integrity, and apply pending migrations if found.
3. **goal-3: Engine Compilation & Verification**: Run CMake configure and build in Release mode using all required flags, verify build output with 0 errors.
4. **goal-4: Service Startup & Health Validation**: Reset systemd failure counters, start `turtle-realmd` and `turtle-mangosd`, verify status and startup logs in journalctl.
