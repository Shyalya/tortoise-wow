---
description: Rules for CMake building, systemd user services, and conditional database updates for Turtle-WoW
globs: ["CMakeLists.txt", "src/**", "sql/**", "run/**"]
alwaysApply: true
---

# Build and Operational Rules

1. **Compilation Flags**:
   - Always include `-DBUILD_PLAYERBOTS=ON`, `-DCMAKE_BUILD_TYPE=Release`, `-DUSE_EXTRACTORS=ON`, and `-DALLOW_TURTLE_ADDONS=ON` when configuring CMake.
   - Build target binary lands at `build/src/mangosd/mangosd`.

2. **Systemd Services**:
   - User units: `turtle-mangosd.service` and `turtle-realmd.service` (no sudo, use `systemctl --user`).
   - If services crash repeatedly, clear rate limits with `systemctl --user reset-failed turtle-mangosd turtle-realmd`.

3. **Database Rules (Conditional Only)**:
   - Database user: `mangos:mangos` on `127.0.0.1:3306`.
   - Never run unprompted database queries or loops.
   - `tw_char` must always have `character_inventory_copy` (required for weekly honor maintenance).
   - Database migrations are only executed if incoming git commits explicitly include new `.sql` files. Otherwise, do not touch the database.
