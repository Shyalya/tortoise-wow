#!/usr/bin/env bash
set -euo pipefail

ETC="${TURTLE_HOME:-/opt/turtle}/etc"
DATA_DIR="${DATA_DIR:-/opt/turtle/data}"
LOGS_DIR="${LOGS_DIR:-/opt/turtle/logs}"
SQL_DIR="${SQL_DIR:-/opt/turtle/sql}"
MODULE_CONFIG_TEMPLATE_DIR="${TURTLE_HOME:-/opt/turtle}/module-configs"

DB_HOST="${DB_HOST:-db}"
DB_PORT="${DB_PORT:-3306}"
DB_USER="${DB_USER:-mangos}"
DB_PASSWORD="${DB_PASSWORD:-mangos}"
DB_LOGIN="${DB_LOGIN:-tw_logon}"
DB_WORLD="${DB_WORLD:-tw_world}"
DB_CHAR="${DB_CHAR:-tw_char}"
DB_LOGS="${DB_LOGS:-tw_logs}"

WORLD_PORT="${WORLD_PORT:-8090}"
REALM_PORT="${REALM_PORT:-3724}"
REALM_ID="${REALM_ID:-1}"
BIND_IP="${BIND_IP:-0.0.0.0}"

LOG_SQL="${LOG_SQL:-0}"
AUTO_UPDATE="${DATABASE_AUTOUPDATE_ENABLED:-1}"
LFT_BOTFILL="${LFT_BOTFILL_ENABLE:-1}"
SOLO_DUNGEON_REPOP="${SOLO_DUNGEON_REPOP_ALIVE_ENABLE:-1}"
LEECH_ENABLE="${LEECH_ENABLE:-1}"

AI_PLAYERBOT_ENABLED="${AI_PLAYERBOT_ENABLED:-1}"
AI_MIN_RANDOM_BOTS="${AI_MIN_RANDOM_BOTS:-10}"
AI_MAX_RANDOM_BOTS="${AI_MAX_RANDOM_BOTS:-10}"

DB_INFO() {
  local db="$1"
  printf '%s;%s;%s;%s;%s' "${DB_HOST}" "${DB_PORT}" "${DB_USER}" "${DB_PASSWORD}" "${db}"
}

set_conf() {
  local file="$1" key="$2" value="$3"
  if grep -qE "^[[:space:]]*${key}[[:space:]]*=" "${file}"; then
    sed -i -E "s|^[[:space:]]*${key}[[:space:]]*=.*|${key} = ${value}|" "${file}"
  else
    printf '\n%s = %s\n' "${key}" "${value}" >> "${file}"
  fi
}

ensure_conf() {
  local dist="$1" conf="$2"
  if [[ ! -f "${conf}" ]]; then
    if [[ ! -f "${dist}" ]]; then
      echo "Missing config template: ${dist}" >&2
      exit 1
    fi
    cp "${dist}" "${conf}"
  fi
}

mkdir -p "${ETC}"

ensure_conf "${ETC}/mangosd.conf.dist" "${ETC}/mangosd.conf"
ensure_conf "${ETC}/realmd.conf.dist" "${ETC}/realmd.conf"

if [[ -f "${ETC}/aiplayerbot.conf.dist" ]]; then
  ensure_conf "${ETC}/aiplayerbot.conf.dist" "${ETC}/aiplayerbot.conf"
fi
if [[ -f "${ETC}/ahbot.conf.dist" ]]; then
  ensure_conf "${ETC}/ahbot.conf.dist" "${ETC}/ahbot.conf"
fi

# Module configs are installed as .conf.dist files. Compose normally bind-mounts
# ./config over ${ETC}, so also check the image-side template directory copied by
# the Dockerfile; otherwise the mount would hide the template before it could be
# materialized as the .conf file that Config::LoadModulesConfigs expects.
MODULE_ETC="${ETC}/modules"
mkdir -p "${MODULE_ETC}"
for dist in "${MODULE_ETC}"/*.conf.dist "${MODULE_CONFIG_TEMPLATE_DIR}"/*.conf.dist; do
  [[ -f "${dist}" ]] || continue
  ensure_conf "${dist}" "${MODULE_ETC}/$(basename "${dist}" .dist)"
done

# mangosd
set_conf "${ETC}/mangosd.conf" "LoginDatabase.Info" "\"$(DB_INFO "${DB_LOGIN}")\""
set_conf "${ETC}/mangosd.conf" "WorldDatabase.Info" "\"$(DB_INFO "${DB_WORLD}")\""
set_conf "${ETC}/mangosd.conf" "CharacterDatabase.Info" "\"$(DB_INFO "${DB_CHAR}")\""
set_conf "${ETC}/mangosd.conf" "LogsDatabase.Info" "\"$(DB_INFO "${DB_LOGS}")\""
set_conf "${ETC}/mangosd.conf" "DataDir" "\"${DATA_DIR}\""
set_conf "${ETC}/mangosd.conf" "LogsDir" "\"${LOGS_DIR}\""
set_conf "${ETC}/mangosd.conf" "WorldServerPort" "${WORLD_PORT}"
set_conf "${ETC}/mangosd.conf" "BindIP" "\"${BIND_IP}\""
set_conf "${ETC}/mangosd.conf" "RealmID" "${REALM_ID}"
set_conf "${ETC}/mangosd.conf" "LogSQL" "${LOG_SQL}"
set_conf "${ETC}/mangosd.conf" "Database.AutoUpdate.Enabled" "${AUTO_UPDATE}"
set_conf "${ETC}/mangosd.conf" "Database.AutoUpdate.Path" "\"${SQL_DIR}/database_updates/\""
set_conf "${ETC}/mangosd.conf" "Database.AutoUpdate.AuthUpdateName" "\"auth\""
set_conf "${ETC}/mangosd.conf" "Database.AutoUpdate.CharUpdateName" "\"character\""
set_conf "${ETC}/mangosd.conf" "Database.AutoUpdate.WorldUpdateName" "\"world\""
set_conf "${ETC}/mangosd.conf" "LFT.BotFill.Enable" "${LFT_BOTFILL}"
set_conf "${ETC}/mangosd.conf" "SoloDungeonRepopAlive.Enable" "${SOLO_DUNGEON_REPOP}"
set_conf "${ETC}/mangosd.conf" "Leech.Enable" "${LEECH_ENABLE}"

# realmd (note: key name has no dots between LoginDatabase and Info)
set_conf "${ETC}/realmd.conf" "LoginDatabaseInfo" "\"$(DB_INFO "${DB_LOGIN}")\""
set_conf "${ETC}/realmd.conf" "RealmServerPort" "${REALM_PORT}"
set_conf "${ETC}/realmd.conf" "BindIP" "\"${BIND_IP}\""

if [[ -f "${ETC}/aiplayerbot.conf" ]]; then
  set_conf "${ETC}/aiplayerbot.conf" "AiPlayerbot.Enabled" "${AI_PLAYERBOT_ENABLED}"
  set_conf "${ETC}/aiplayerbot.conf" "AiPlayerbot.MinRandomBots" "${AI_MIN_RANDOM_BOTS}"
  set_conf "${ETC}/aiplayerbot.conf" "AiPlayerbot.MaxRandomBots" "${AI_MAX_RANDOM_BOTS}"
fi

mkdir -p "${LOGS_DIR}"
# Writable for the turtle user (configs may be regenerated each start).
chown -R turtle:turtle "${ETC}" "${LOGS_DIR}" 2>/dev/null || true

echo "Configs rendered under ${ETC}"
