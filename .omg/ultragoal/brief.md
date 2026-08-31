# Ultragoal Brief: Upstream Sync (Cycle 8), Migration & Rebuild

## Objective
Safely synchronize the `playerbots-integration-gh` branch with upstream `origin/playerbots-integration-gh` (9 commits including Eluna Lua scripting engine integration with submodule setup, SQL spell script load ordering, and Gnomeregan roster fixes), preserving all local modifications via git stash/pop, compiling the engine in Release mode, and verifying systemd service health.

## Context & Architecture Boundaries
- Master Sync Pipeline defined in `MEMORY.md` (Section 1) and `.omg/rules/build_rules.md`.
- Active database engine: MariaDB 11.8 (`127.0.0.1:3306`, user `mangos:mangos`).
- Services: `turtle-mangosd.service`, `turtle-realmd.service` (systemd user services).
- CMake compilation flags: `-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/sam/server -DBUILD_PLAYERBOTS=ON -DUSE_EXTRACTORS=ON -DALLOW_TURTLE_ADDONS=ON`.
- Submodules: `src/modules/Eluna` must be updated via `git submodule update --init --recursive`.

## Verification Strategy
- Clean daemon shutdown via systemctl before stashing.
- Git pull & fast-forward from origin/playerbots-integration-gh + submodule init.
- Git stash pop with zero unresolved conflicts.
- Audit commit diff for SQL migrations and apply conditionally.
- CMake build completion with 0 errors.
- Systemd service activation and active status verification.
