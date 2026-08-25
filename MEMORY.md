# Tortoise-WoW Project Memory Index

> **Target Version**: Turtle-WoW 1.18.1 (Build 7272)
> **Core Engine**: CMaNGOS-based restoration with native PlayerBots (~1000 persistent bots), custom PvP Honor system, and solo-play scaling enhancements.

---

## 1. Master Remote Sync, Merge & Build Pipeline (MANDATORY SEQUENCE)

Whenever syncing from remote (`playerbots-integration-gh`), the AI assistant and operators **MUST** execute the following 9-step pipeline in exact linear order. Never skip steps, never reorder, and never run ad-hoc commands out of sequence.

```bash
# ----------------------------------------------------------------------
# STEP 1: Stop running server daemons cleanly
# ----------------------------------------------------------------------
systemctl --user stop turtle-mangosd turtle-realmd

# ----------------------------------------------------------------------
# STEP 2: Stash any local uncommitted files (docs, configs, logs)
# ----------------------------------------------------------------------
git stash save "Local modifications before sync"

# ----------------------------------------------------------------------
# STEP 3: Pull and merge latest commits from origin
# ----------------------------------------------------------------------
git pull origin playerbots-integration-gh

# ----------------------------------------------------------------------
# STEP 4: Restore local files back into the working tree (Pop stash)
# ----------------------------------------------------------------------
git stash pop

# ----------------------------------------------------------------------
# STEP 5: Audit incoming commits for required actions
# Check if any .sql migrations, .conf files, or DBC files were touched
# ----------------------------------------------------------------------
git diff HEAD@{1}..HEAD --stat

# ----------------------------------------------------------------------
# STEP 6: Database Migrations (STRICTLY CONDITIONAL - ONLY IF SQL IN DIFF)
# If NO .sql files exist in the diff: SKIP DATABASE COMPLETELY (DO NOT TOUCH DB).
# If NEW .sql files exist: Apply with --force and record in tw_world.migrations (see Section 4).
# ----------------------------------------------------------------------

# ----------------------------------------------------------------------
# STEP 7: Configure and Compile C++ Engine in Release Mode
# ----------------------------------------------------------------------
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/home/sam/server \
  -DBUILD_PLAYERBOTS=ON \
  -DUSE_EXTRACTORS=ON \
  -DALLOW_TURTLE_ADDONS=ON

cmake --build build -j$(nproc)

# ----------------------------------------------------------------------
# STEP 8: Clear systemd rate limits and restart services
# ----------------------------------------------------------------------
systemctl --user reset-failed turtle-mangosd turtle-realmd
systemctl --user start turtle-mangosd turtle-realmd

# ----------------------------------------------------------------------
# STEP 9: Verify operational status and monitor logs
# ----------------------------------------------------------------------
systemctl --user status turtle-mangosd turtle-realmd
journalctl --user -u turtle-mangosd -n 50 --no-pager
```

---

## 2. Inviolable AI Assistant Guardrails

1. **Strict Command Ordering**:
   - Always stop daemons **before** pull.
   - Always stash local modifications **before** pull, and immediately pop stash **after** pull so user work is restored.
   - Always check commit diff **before** deciding if database or config work is needed.
   - Always build with required flags (`-DBUILD_PLAYERBOTS=ON -DALLOW_TURTLE_ADDONS=ON -DCMAKE_BUILD_TYPE=Release`).
2. **Database Protection Lock**:
   - **NEVER** run unprompted, exploratory, or out-of-order queries (`SELECT`, `SHOW TABLES`, loops, updates) against MariaDB (`tw_world`, `tw_char`, `tw_logon`, `tw_logs`).
   - Database operations are strictly done in **Step 6** and **ONLY** when new `.sql` migration files are introduced by incoming commits.
   - If no `.sql` files are in the commit diff, the database **MUST NOT BE TOUCHED**.

---

## 3. Executive Architecture Overview

```bash
[ Game Client (1.18.1.7272) ]
              |
              +---> realmd (Port 3724) -------> [ tw_logon ] (Account auth & realmlist)
              |
              +---> mangosd (Port 8090) ------> [ tw_world ] (World content & scripts)
                         |               -----> [ tw_char ]  (Player & Bot characters)
                         |               -----> [ tw_logs ]  (System logs & audits)
                         |
                   [ PlayerBots Module ]
                   (ike3 / r-o-sh integrated engine)
```

| Component | Path / Reference | Description |
| --- | --- | --- |
| **World Daemon** | `build/src/mangosd/mangosd` | Core simulation daemon, handles player/bot entities, movement, combat, instances |
| **Auth Daemon** | `build/src/realmd/realmd` | Authentication & realm routing service |
| **Run Working Dir** | `/home/sam/tortoise-wow/run` | Active runtime configs (`mangosd.conf`, `realmd.conf`, `aiplayerbot.conf`) |
| **Systemd Units** | `~/.config/systemd/user/` | `turtle-mangosd.service`, `turtle-realmd.service`, `turtle-logrotate.timer` |
| **Database Engine** | MariaDB 11.8 (`127.0.0.1:3306`) | 4 Databases: `tw_world`, `tw_char`, `tw_logon`, `tw_logs` (User: `mangos:mangos`) |

---

## 4. Database Operations & Migration Reference (Done Only If Necessary)

### A. Applying World Database Migrations (`tw_world`)

`Database.AutoUpdate.Enabled = 0` is intentional. When new SQL updates appear in `sql/database_updates/world/`:

```bash
# 1. Run new migration with --force (skips duplicate errors safely)
mariadb --force -h 127.0.0.1 -u mangos -pmangos tw_world < sql/database_updates/world/NEW_FILE.sql

# 2. Mark migration as applied in tracker table
mariadb -h 127.0.0.1 -u mangos -pmangos -e "INSERT IGNORE INTO tw_world.migrations (Name, Hash, AppliedAt) VALUES ('NEW_FILE_NAME','manual',NOW());"
```

### B. Applying Character Database Migrations (`tw_char`)

When new character SQL updates appear in `sql/database_updates/character/`:

```bash
mariadb -h 127.0.0.1 -u mangos -pmangos tw_char < sql/database_updates/character/NEW_FILE.sql
```

### C. Weekly Honor Maintenance Table (`tw_char`)

- `HonorMaintenancer::DoMaintenance()` executes at the start of each weekly reset and requires `character_inventory_copy`.
- Ensure table exists:

```sql
CREATE TABLE IF NOT EXISTS tw_char.character_inventory_copy LIKE tw_char.character_inventory;
```

---

## 5. Topic References & Documentation Library

- [Build and Service Lifecycle SOP](file:///home/sam/tortoise-wow/.omg/memory/build_and_service_lifecycle.md)
- [Operational Build Rules](file:///home/sam/tortoise-wow/.omg/rules/build_rules.md)
- [Command Discipline & Safety Rules](file:///home/sam/tortoise-wow/.omg/rules/command_discipline.md)
- [Systemd Commands Quick Reference](file:///home/sam/tortoise-wow/commands.md)
- [Game Master (GM) In-Game Commands](file:///home/sam/tortoise-wow/gm_commands.md)
- [Dungeon Clear AI Module Reference](file:///home/sam/tortoise-wow/references/15_dungeon_clear_module.md)
- [Playerbots Quickstart Guide](file:///home/sam/tortoise-wow/PLAYERBOTS_QUICKSTART.md)
- [Linux Installation Walkthrough](file:///home/sam/tortoise-wow/INSTALL-LINUX.md)
