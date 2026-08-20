# Tortoise-WoW Project Memory Index

> **Target Version**: Turtle-WoW 1.18.1 (Build 7272)
> **Core Engine**: CMaNGOS-based restoration with native PlayerBots (~1000 persistent bots), custom PvP Honor system, and solo-play scaling enhancements.

---

## 1. Executive Architecture Overview

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
| **Database Engine** | MariaDB 11.8 (`127.0.0.1:3306`) | 4 Databases: `tw_world`, `tw_char`, `tw_logon`, `tw_logs` (User: `mangos`) |

---

## 2. Critical Operational & Stability Rules

### A. Weekly Honor Maintenance (`tw_char`)

- **Mechanism**: `HonorMaintenancer::DoMaintenance()` executes at the start of each weekly reset. It calls `ObjectMgr::BackupCharacterInventory()` which performs:

  ```sql
  TRUNCATE `character_inventory_copy`;
  INSERT INTO `character_inventory_copy` SELECT * FROM `character_inventory`;
  ```

- **Mandatory Schema**: `tw_char.character_inventory_copy` must always exist:

  ```sql
  CREATE TABLE IF NOT EXISTS tw_char.character_inventory_copy LIKE tw_char.character_inventory;
  ```

- **Symptom if missing**: Immediate server abort (`SIGABRT` / `Assertion in HandleMySQLError failed: false`) upon launching when the weekly maintenance threshold has passed.

### B. CMake Compilation & Build Flags

- Always configure with:

  ```bash
  cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/home/sam/server \
    -DBUILD_PLAYERBOTS=ON \
    -DUSE_EXTRACTORS=ON \
    -DALLOW_TURTLE_ADDONS=ON
  ```

- **Playerbots**: `BUILD_PLAYERBOTS` defaults to `OFF` upstream. Must be set to `ON`.
- **Addon Compatibility**: `ALLOW_TURTLE_ADDONS=ON` must remain enabled, or entering the world aborts client-side with "interface corrupt".
- **Performance**: Always use `Release` mode (`Debug` binaries exceed 600MB and degrade under bot load).

### C. Database Migration Protocol (`tw_world`)

- **AutoUpdate is intentionally disabled** (`Database.AutoUpdate.Enabled = 0`) because `sql/base` snapshot data overlaps with early migration files.
- When pulling upstream changes:
  1. Import any new base definitions:

     ```bash
     mariadb -h 127.0.0.1 -u mangos -pmangos tw_world < sql/base/tw_world_custom_merchant.sql
     mariadb -h 127.0.0.1 -u mangos -pmangos tw_world < sql/base/tw_world_itemextendedcost.sql
     ```

  2. Run migration updates with `--force` to skip pre-existing duplicate entries while executing schema modifications:

     ```bash
     for f in sql/database_updates/world/*.sql; do
       mariadb --force -h 127.0.0.1 -u mangos -pmangos tw_world < "$f"
     done
     ```

  3. Mark migrations as applied:

     ```bash
     for f in sql/database_updates/world/*.sql; do
       n=$(basename "$f" .sql)
       mariadb -h 127.0.0.1 -u mangos -pmangos -e "INSERT IGNORE INTO tw_world.migrations (Name, Hash, AppliedAt) VALUES ('$n','manual',NOW());"
     done
     ```

  4. Run character database updates:

     ```bash
     for f in sql/database_updates/character/*.sql; do
       mariadb -h 127.0.0.1 -u mangos -pmangos tw_char < "$f"
     done
     ```

---

## 3. Standard Operating Procedure (Pull, Rebuild, Run)

Whenever updating from remote (`playerbots-integration-gh`):

```bash
# 1. Stop active services
systemctl --user stop turtle-mangosd turtle-realmd

# 2. Stash local changes & pull
git stash save "Local modifications before sync"
git pull
git stash pop

# 3. Clean build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/sam/server -DBUILD_PLAYERBOTS=ON -DUSE_EXTRACTORS=ON
cmake --build build -j$(nproc)

# 4. Clear service rate limits & restart
systemctl --user reset-failed turtle-mangosd turtle-realmd
systemctl --user start turtle-mangosd turtle-realmd

# 5. Verify status & logs
systemctl --user status turtle-mangosd turtle-realmd
journalctl --user -u turtle-mangosd -f
```

---

## 4. Topic References & Documentation

- [Build and Service Lifecycle Standard Operating Procedure](file:///home/sam/tortoise-wow/.omg/memory/build_and_service_lifecycle.md)
- [Operational Build & Database Rules](file:///home/sam/tortoise-wow/.omg/rules/build_rules.md)
- [Systemd Commands Quick Reference](file:///home/sam/tortoise-wow/commands.md)
- [Linux Installation & Troubleshooting Walkthrough](file:///home/sam/tortoise-wow/INSTALL-LINUX.md)
- [Playerbots Quickstart Guide](file:///home/sam/tortoise-wow/PLAYERBOTS_QUICKSTART.md)
- [Game Master (GM) In-Game Commands](file:///home/sam/tortoise-wow/gm_commands.md)
