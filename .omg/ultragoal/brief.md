# Ultragoal Brief: Upstream Sync (Cycle 5), Migration & Rebuild

## Objective
Safely synchronize the `playerbots-integration-gh` branch with upstream `origin/playerbots-integration-gh` (31 commits including Northwind quest consolidation, spell_extra table creation & DBC spell loader switch, SFK/WC dungeon clear routes, and core memory fixes), preserving all local modifications via git stash/pop, applying incoming database migrations (`20260611110845_world.sql` and `20260817211652_world.sql`), compiling the engine in Release mode, and verifying systemd service health.

## Context & Architecture Boundaries
- Master Sync Pipeline defined in `MEMORY.md` (Section 1) and `.omg/rules/build_rules.md`.
- Active database engine: MariaDB 11.8 (`127.0.0.1:3306`, user `mangos:mangos`).
- Services: `turtle-mangosd.service`, `turtle-realmd.service` (systemd user services).
- CMake compilation flags: `-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/sam/server -DBUILD_PLAYERBOTS=ON -DUSE_EXTRACTORS=ON -DALLOW_TURTLE_ADDONS=ON`.

## Verification Strategy
- Clean daemon shutdown via systemctl before stashing.
- Git pull & rebase from origin/playerbots-integration-gh.
- Git stash pop with zero unresolved conflicts.
- Apply incoming world migrations with `--force` and log in `tw_world.migrations`.
- CMake build completion with 0 errors.
- Systemd service activation and active status verification.
