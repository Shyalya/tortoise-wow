#!/usr/bin/env bash
# Repair migration metadata after a manual import or a legacy init.
# Safe to re-run. Does not re-import SQL; only use it when the SQL is already
# present in the databases.
set -euo pipefail

SQL_ROOT="${SQL_DIR:-/opt/turtle/sql}"
DB_HOST="${DB_HOST:-db}"
DB_PORT="${DB_PORT:-3306}"
DB_ROOT_PASSWORD="${DB_ROOT_PASSWORD:-${MYSQL_ROOT_PASSWORD:-}}"
DB_USER="${DB_USER:-mangos}"
DB_PASSWORD="${DB_PASSWORD:-mangos}"
DB_WORLD="${DB_WORLD:-tw_world}"
DB_CHAR="${DB_CHAR:-tw_char}"

mysql_cmd() {
  if [[ -n "${DB_ROOT_PASSWORD}" ]]; then
    mysql -h"${DB_HOST}" -P"${DB_PORT}" -uroot -p"${DB_ROOT_PASSWORD}" --protocol=TCP "$@"
  else
    mysql -h"${DB_HOST}" -P"${DB_PORT}" -u"${DB_USER}" -p"${DB_PASSWORD}" --protocol=TCP "$@"
  fi
}

ensure_migration_module() {
  local db="$1"
  local module_column
  module_column="$(mysql_cmd -N -B -e "SHOW COLUMNS FROM \`${db}\`.migrations LIKE 'Module';")"
  if [[ -z "${module_column}" ]]; then
    mysql_cmd -e "ALTER TABLE \`${db}\`.migrations ADD COLUMN \`Module\` VARCHAR(255) NOT NULL DEFAULT '' COLLATE 'utf8_general_ci' AFTER \`Name\`;"
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

record_migrations() {
  local db="$1" module="$2" dir="$3"
  local f n h
  while IFS= read -r f; do
    [[ -n "${f}" ]] || continue
    n="$(basename "${f}" .sql)"
    h="$(sha1sum "${f}" | awk '{ print toupper($1) }')"
    mysql_cmd -e "UPDATE \`${db}\`.migrations SET Module='${module}' WHERE Hash='${h}' AND (Module='' OR Module IS NULL);"
    if [[ -z "$(mysql_cmd -N -B -e "SELECT 1 FROM \`${db}\`.migrations WHERE Module='${module}' AND Hash='${h}' LIMIT 1;")" ]]; then
      mysql_cmd -e "INSERT INTO \`${db}\`.migrations (Name, Module, Hash, AppliedAt) VALUES ('${n}','${module}','${h}',NOW());"
    fi
  done < <(migration_files "${dir}")
}

shopt -s nullglob
ensure_migration_module "${DB_WORLD}"
ensure_migration_module "${DB_CHAR}"

echo "Recording world migration hashes..."
record_migrations "${DB_WORLD}" world "${SQL_ROOT}/database_updates/world"
echo "Recording character migration hashes..."
record_migrations "${DB_CHAR}" character "${SQL_ROOT}/database_updates/character"

world_count="$(mysql_cmd -N -e "SELECT COUNT(*) FROM \`${DB_WORLD}\`.migrations;")"
char_count="$(mysql_cmd -N -e "SELECT COUNT(*) FROM \`${DB_CHAR}\`.migrations;")"
echo "Done. migration rows: world=${world_count}, character=${char_count}"
