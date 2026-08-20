# Build & Service Lifecycle Standard Operating Procedure

This document outlines the end-to-end operational lifecycle for building, synchronizing, maintaining, and running the **Tortoise-WoW** server ecosystem.

---

## 1. Upstream Synchronization Workflow

When pulling upstream updates from `origin/playerbots-integration-gh`:

```bash
# 1. Stop active daemons to avoid writing to busy binaries
systemctl --user stop turtle-mangosd turtle-realmd

# 2. Stash local modifications (custom scripts, ignore rules, tweaks)
git stash save "Local modifications before upstream sync"

# 3. Pull latest commits from upstream
git pull

# 4. Re-apply local stashed changes
git stash pop
```

---

## 2. Compilation & Toolchain Specification

### Required Toolchain

- **Compiler**: GCC 14+ (or Clang with `-DUSE_PCH=OFF`)
- **Build System**: CMake 3.30+ & Ninja / GNU Make
- **Core Dependencies**: ACE 8.x (`libace-dev`), Boost 1.83+ (`libboost-all-dev`), MariaDB Client (`default-libmysqlclient-dev`), OpenSSL 3.x, ZLib, Bzip2.

### CMake Build Command

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/home/sam/server \
  -DBUILD_PLAYERBOTS=ON \
  -DUSE_EXTRACTORS=ON \
  -DALLOW_TURTLE_ADDONS=ON

cmake --build build -j$(nproc)
```

> **Critical Flags**:
>
> - `-DBUILD_PLAYERBOTS=ON`: Defaults to `OFF` in base repo. Required for the bot AI submodule.
> - `-DCMAKE_BUILD_TYPE=Release`: Ensures performance optimization essential for ~1000 active bots.
> - `-DALLOW_TURTLE_ADDONS=ON`: Prevents "interface corrupt" client crash on login.

---

## 3. Database Schema Maintenance

The server connects to MariaDB at `127.0.0.1:3306` with user credentials `mangos:mangos`.

### 3.1 Character Database (`tw_char`)

1. **Weekly Honor Maintenance**:
   - `HonorMaintenancer::DoMaintenance()` runs at weekly server reset and truncates/copies `character_inventory` to `character_inventory_copy`.
   - **Verification / Creation**:

     ```sql
     CREATE TABLE IF NOT EXISTS tw_char.character_inventory_copy LIKE tw_char.character_inventory;
     ```

2. **PvP Currency Schema**:
   - Required for the updated honor system:

     ```bash
     for f in sql/database_updates/character/*.sql; do
       mariadb -h 127.0.0.1 -u mangos -pmangos tw_char < "$f"
     done
     ```

### 3.2 World Database (`tw_world`)

Because `sql/base` snapshot data overlaps with early migrations, automatic in-engine migration execution is disabled (`Database.AutoUpdate.Enabled = 0`).

1. **Import new base tables**:

   ```bash
   mariadb -h 127.0.0.1 -u mangos -pmangos tw_world < sql/base/tw_world_custom_merchant.sql
   mariadb -h 127.0.0.1 -u mangos -pmangos tw_world < sql/base/tw_world_itemextendedcost.sql
   ```

2. **Apply migrations tolerating duplicate-key errors**:

   ```bash
   for f in sql/database_updates/world/*.sql; do
     mariadb --force -h 127.0.0.1 -u mangos -pmangos tw_world < "$f"
   done
   ```

3. **Record applied migrations**:

   ```bash
   for f in sql/database_updates/world/*.sql; do
     n=$(basename "$f" .sql)
     mariadb -h 127.0.0.1 -u mangos -pmangos -e "INSERT IGNORE INTO tw_world.migrations (Name, Hash, AppliedAt) VALUES ('$n','manual',NOW());"
   done
   ```

---

## 4. Systemd Service Management

The daemons are managed as **systemd user services** (`~/.config/systemd/user/`):

| Service | Target Binary / Working Directory | Description |
| --- | --- | --- |
| `turtle-mangosd.service` | `build/src/mangosd/mangosd` (`run/`) | World simulation daemon |
| `turtle-realmd.service` | `build/src/realmd/realmd` (`run/`) | Auth / realm gateway daemon |
| `turtle-logrotate.timer` | Runs log rotation | Automatically rotates `logs/bot_events.csv` |

### Service Command Summary

```bash
# Clear any crash loop rate-limits
systemctl --user reset-failed turtle-mangosd turtle-realmd

# Start services
systemctl --user start turtle-mangosd turtle-realmd

# Stop services (frees CPU when not playing)
systemctl --user stop turtle-mangosd turtle-realmd

# Restart services
systemctl --user restart turtle-mangosd turtle-realmd

# Live log stream
journalctl --user -u turtle-mangosd -f

# Review last 5 minutes of logs
journalctl --user -u turtle-mangosd --since "5 min ago" --no-pager
```
