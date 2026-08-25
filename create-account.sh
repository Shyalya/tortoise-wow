#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

ACCOUNT_NAME="${1:-nick}"
if [[ $# -ge 2 ]]; then
    ACCOUNT_PASSWORD="$2"
elif [[ -t 0 ]]; then
    read -r -s -p "Password for ${ACCOUNT_NAME}: " ACCOUNT_PASSWORD
    printf '\n'
else
    printf 'Usage: %s [username] <password>\n' "$0" >&2
    exit 2
fi

if [[ $# -gt 2 ]]; then
    printf 'Usage: %s [username] <password>\n' "$0" >&2
    exit 2
fi

if ! command -v docker >/dev/null 2>&1; then
    printf 'docker is not installed or is not on PATH.\n' >&2
    exit 127
fi

if ! docker info >/dev/null 2>&1; then
    printf '%s\n' \
        'Docker is unavailable to this user.' \
        'If this says permission denied for /var/run/docker.sock, run:' \
        "  sudo usermod -aG docker $(id -un)" \
        'Then log out and back in (or run: newgrp docker) and retry.' >&2
    exit 1
fi

docker compose exec -T -u turtle mangosd \
    sh -c 'exec 3>/opt/turtle/run/mangosd.in; printf "account create %s %s\n" "$1" "$2" >&3; sleep 1; exec 3>&-' \
    -- "${ACCOUNT_NAME}" "${ACCOUNT_PASSWORD}"

# `account create` does not replace the password when the account already exists.
docker compose exec -T -u turtle mangosd \
    sh -c 'exec 3>/opt/turtle/run/mangosd.in; printf "account set password %s %s %s\n" "$1" "$2" "$2" >&3; sleep 1; exec 3>&-' \
    -- "${ACCOUNT_NAME}" "${ACCOUNT_PASSWORD}"
