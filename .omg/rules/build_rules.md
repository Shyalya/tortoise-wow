---
description: Rules for CMake building, systemd user services, and database updates for Turtle-WoW
globs: ["CMakeLists.txt", "src/**", "sql/**", "run/**"]
alwaysApply: true
---

# Build and Operational Rules

1. **Compilation Flags**:
   - Always include `-DBUILD_PLAYERBOTS=ON` and `-DCMAKE_BUILD_TYPE=Release` when configuring CMake.
   - Build target binary lands at `build/src/mangosd/mangosd`.

2. **Systemd Services**:
   - User units: `turtle-mangosd.service` and `turtle-realmd.service` (no sudo, use `systemctl --user`).
   - If services crash repeatedly, clear rate limits with `systemctl --user reset-failed turtle-mangosd turtle-realmd`.

3. **Database Rules**:
   - Database user: `mangos:mangos` on `127.0.0.1:3306`.
   - `tw_char` must always have `character_inventory_copy` (required for weekly honor maintenance).
   - `tw_world` migrations must be applied with `mariadb --force` and recorded into `tw_world.migrations` due to `Database.AutoUpdate.Enabled = 0`.
