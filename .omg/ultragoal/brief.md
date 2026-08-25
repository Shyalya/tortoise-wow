# Ultragoal Brief: Upstream Sync, Stash Restoration, Migration & Rebuild

## Objective
Safely synchronize the `playerbots-integration-gh` branch with upstream `origin/playerbots-integration-gh`, preserving all local modifications via git stash/pop, auditing and applying new incoming database migrations (`20260821154713_world.sql` & `20260821203635_world.sql`), compiling the engine in Release mode, and verifying systemd service health.

## Context & Architecture Boundaries
- Master Sync Pipeline defined in `MEMORY.md` (Section 1) and `.omg/rules/build_rules.md`.
- Active database engine: MariaDB 11.8 (`127.0.0.1:3306`, user `mangos:mangos`).
- Services: `turtle-mangosd.service`, `turtle-realmd.service` (systemd user services).
- CMake compilation flags: `-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/sam/server -DBUILD_PLAYERBOTS=ON -DUSE_EXTRACTORS=ON -DALLOW_TURTLE_ADDONS=ON`.

## Verification Strategy
- Git status & diff checks before and after sync/stash pop.
- Database migration execution log & recording in `tw_world.migrations`.
- CMake build completion with 0 errors.
- Systemd service activation and clean startup logs via journalctl.
