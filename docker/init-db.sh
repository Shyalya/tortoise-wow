#!/usr/bin/env bash
set -euo pipefail

MARKER_DIR="${INIT_MARKER_DIR:-/var/lib/turtle-init}"
MARKER_FILE="${MARKER_DIR}/initialized"
SQL_ROOT="${SQL_DIR:-/opt/turtle/sql}"

DB_HOST="${DB_HOST:-db}"
DB_PORT="${DB_PORT:-3306}"
DB_ROOT_PASSWORD="${DB_ROOT_PASSWORD:-${MYSQL_ROOT_PASSWORD:-}}"
DB_USER="${DB_USER:-mangos}"
DB_PASSWORD="${DB_PASSWORD:-mangos}"
DB_LOGIN="${DB_LOGIN:-tw_logon}"
DB_WORLD="${DB_WORLD:-tw_world}"
DB_CHAR="${DB_CHAR:-tw_char}"
DB_LOGS="${DB_LOGS:-tw_logs}"

REALM_NAME="${REALM_NAME:-TurtleWoW}"
REALM_ADDRESS="${REALM_ADDRESS:-127.0.0.1}"
WORLD_PORT="${WORLD_PORT:-8090}"
REALM_ID="${REALM_ID:-1}"

PLAYERBOTS_BUILT="${PLAYERBOTS_BUILT:-ON}"

shopt -s nullglob
echo "Start db-init..."
ensure_migration_module() {
  local db="$1"
  local module_column
  module_column="$(mysql_root -N -B -e "SHOW COLUMNS FROM \`${db}\`.migrations LIKE 'Module';")"
  if [[ -z "${module_column}" ]]; then
    mysql_root -e "ALTER TABLE \`${db}\`.migrations ADD COLUMN \`Module\` VARCHAR(255) NOT NULL DEFAULT '' COLLATE 'utf8_general_ci' AFTER \`Name\`;"
  fi
}

migration_files() {
  local dir="$1"
  local f
  local files=("${dir}"/*.sql)
  for f in "${files[@]}"; do
    printf '%s\n' "${f}"
  done
}

normalize_migration_modules() {
  local db="$1" module="$2" dir="$3"
  local f h
  while IFS= read -r f; do
    [[ -n "${f}" ]] || continue
    h="$(sha1sum "${f}" | awk '{ print toupper($1) }')"
    if [[ -z "${module}" ]]; then
      # Core migrations are keyed without a module. Repair rows written by
      # older init scripts that incorrectly used the database name here.
      mysql_root -e "UPDATE \`${db}\`.migrations SET Module='' WHERE Hash='${h}';"
    else
      mysql_root -e "UPDATE \`${db}\`.migrations SET Module='${module}' WHERE Hash='${h}' AND (Module='' OR Module IS NULL);"
    fi
  done < <(migration_files "${dir}")
}

apply_migrations() {
  local db="$1" module="$2" dir="$3"
  local f n h
  while IFS= read -r f; do
    [[ -n "${f}" ]] || continue
    n="$(basename "${f}" .sql)"
    echo "  -> ${module:-core}/${n}"
    mysql_root --force "${db}" < "${f}" || true
    h="$(sha1sum "${f}" | awk '{ print toupper($1) }')"
    mysql_root -e "INSERT INTO \`${db}\`.migrations (Name, Module, Hash, AppliedAt) VALUES ('${n}','${module}','${h}',NOW());"
  done < <(migration_files "${dir}")
}

if [[ -z "${DB_ROOT_PASSWORD}" ]]; then
  echo "DB_ROOT_PASSWORD (or MYSQL_ROOT_PASSWORD) is required." >&2
  exit 1
fi

mysql_root() {
  mysql -h"${DB_HOST}" -P"${DB_PORT}" -uroot -p"${DB_ROOT_PASSWORD}" --protocol=TCP "$@"
}

echo "Waiting for MariaDB at ${DB_HOST}:${DB_PORT}..."
for i in $(seq 1 90); do
  if mysql_root -e "SELECT 1" &>/dev/null; then
    break
  fi
  if [[ "${i}" -eq 90 ]]; then
    echo "MariaDB did not become ready in time." >&2
    exit 1
  fi
  sleep 2
done
echo "MariaDB is ready."

if [[ -f "${MARKER_FILE}" ]]; then
  echo "Init marker found (${MARKER_FILE}); skipping database import."
  echo "Normalizing legacy migration metadata..."
  ensure_migration_module "${DB_WORLD}"
  ensure_migration_module "${DB_CHAR}"
  normalize_migration_modules "${DB_WORLD}" "" "${SQL_ROOT}/database_updates/world"
  normalize_migration_modules "${DB_CHAR}" "" "${SQL_ROOT}/database_updates/character"
  exit 0
fi

if [[ ! -f "${SQL_ROOT}/create_databases.sql" ]]; then
  echo "Missing ${SQL_ROOT}/create_databases.sql" >&2
  exit 1
fi

echo "Creating databases and base schemas..."
mysql_root < "${SQL_ROOT}/create_databases.sql"

# BackupCharacterInventory copies rows with INSERT ... SELECT * and therefore
# requires a structurally identical snapshot table in the character database.
character_inventory_copy_sql="${SQL_ROOT}/character-inventory-copy.sql"
if [[ ! -f "${character_inventory_copy_sql}" ]]; then
  echo "Missing ${character_inventory_copy_sql}" >&2
  exit 1
fi
echo "Ensuring character_inventory_copy exists..."
mysql_root "${DB_CHAR}" < "${character_inventory_copy_sql}"

echo "Creating application user '${DB_USER}' and grants..."
mysql_root <<SQL
CREATE USER IF NOT EXISTS '${DB_USER}'@'%' IDENTIFIED BY '${DB_PASSWORD}';
ALTER USER '${DB_USER}'@'%' IDENTIFIED BY '${DB_PASSWORD}';
GRANT ALL PRIVILEGES ON \`${DB_LOGIN}\`.* TO '${DB_USER}'@'%';
GRANT ALL PRIVILEGES ON \`${DB_WORLD}\`.* TO '${DB_USER}'@'%';
GRANT ALL PRIVILEGES ON \`${DB_CHAR}\`.* TO '${DB_USER}'@'%';
GRANT ALL PRIVILEGES ON \`${DB_LOGS}\`.* TO '${DB_USER}'@'%';
FLUSH PRIVILEGES;
SQL

echo "Importing world content from sql/base (this can take several minutes)..."
base_files=("${SQL_ROOT}"/base/*.sql)
if [[ "${#base_files[@]}" -eq 0 ]]; then
  echo "No SQL files found under ${SQL_ROOT}/base" >&2
  exit 1
fi
for f in "${base_files[@]}"; do
  echo "  -> $(basename "${f}")"
  mysql_root "${DB_WORLD}" < "${f}"
done

echo "Applying database migrations with --force (duplicate keys expected)..."
ensure_migration_module "${DB_WORLD}"
ensure_migration_module "${DB_CHAR}"
apply_migrations "${DB_CHAR}" "" "${SQL_ROOT}/database_updates/character"
apply_migrations "${DB_WORLD}" "" "${SQL_ROOT}/database_updates/world"

# Verify a known schema change from migrations landed.
col_count="$(mysql_root -N -e "SELECT COUNT(*) FROM information_schema.COLUMNS WHERE TABLE_SCHEMA='${DB_WORLD}' AND TABLE_NAME='spell_template' AND COLUMN_NAME='script_name';")"
if [[ "${col_count}" != "1" ]]; then
  echo "WARNING: spell_template.script_name not found after migrations (got count=${col_count})." >&2
fi

# Playerbot tables (only when this image was built with BUILD_PLAYERBOTS=ON)
normalized="$(echo "${PLAYERBOTS_BUILT}" | tr '[:lower:]' '[:upper:]')"
if [[ "${normalized}" == "ON" || "${normalized}" == "1" || "${normalized}" == "TRUE" ]]; then
  PB_SQL="${SQL_ROOT}/playerbots"
  if [[ -d "${PB_SQL}" ]]; then
    echo "Importing playerbots world SQL..."
    cat "${PB_SQL}"/world/*.sql "${PB_SQL}"/world/classic/*.sql | mysql_root "${DB_WORLD}"
    echo "Importing playerbots characters SQL..."
    cat "${PB_SQL}"/characters/*.sql | mysql_root "${DB_CHAR}"
  else
    echo "PLAYERBOTS_BUILT=${PLAYERBOTS_BUILT} but ${PB_SQL} is missing." >&2
    exit 1
  fi
else
  echo "Skipping playerbots SQL (PLAYERBOTS_BUILT=${PLAYERBOTS_BUILT})."
fi

echo "Inserting realmlist row..."
mysql_root <<SQL
DELETE FROM ${DB_LOGIN}.realmlist;
INSERT INTO ${DB_LOGIN}.realmlist
  (id, name, address, port, icon, realmflags, timezone, allowedSecurityLevel, realmbuilds)
VALUES
  (${REALM_ID}, '${REALM_NAME}', '${REALM_ADDRESS}', ${WORLD_PORT}, 0, 0, 1, 0, '7272');
SQL

mkdir -p "${MARKER_DIR}"
date -u +"%Y-%m-%dT%H:%M:%SZ" > "${MARKER_FILE}"
echo "Database init complete."
