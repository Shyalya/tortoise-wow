#!/usr/bin/env bash
set -euo pipefail

ROLE="${1:-mangosd}"
shift || true

case "${ROLE}" in
  db-init|init-db)
    exec /usr/local/bin/init-db.sh "$@"
    ;;
  realmd)
    /usr/local/bin/render-config.sh
    echo "Starting realmd..."
    exec gosu turtle /opt/turtle/bin/realmd -c /opt/turtle/etc/realmd.conf "$@"
    ;;
  mangosd)
    /usr/local/bin/render-config.sh
    RUN_DIR="${TURTLE_HOME:-/opt/turtle}/run"
    FIFO="${MANGOSD_FIFO:-${RUN_DIR}/mangosd.in}"
    mkdir -p "$(dirname "${FIFO}")"
    chown turtle:turtle "$(dirname "${FIFO}")"
    rm -f "${FIFO}"
    # Open the FIFO as the same user that owns it. Creating it in sticky /tmp,
    # chowning to turtle, then opening as root fails when fs.protected_fifos=1.
    echo "Starting mangosd (console FIFO: ${FIFO})..."
    exec gosu turtle bash -c '
      set -euo pipefail
      FIFO="$1"
      shift
      mkfifo "${FIFO}"
      exec 3<>"${FIFO}"
      exec /opt/turtle/bin/mangosd -c /opt/turtle/etc/mangosd.conf "$@" <&3
    ' bash "${FIFO}" "$@"
    ;;
  bash|sh)
    exec "$@"
    ;;
  *)
    echo "Unknown role: ${ROLE}" >&2
    echo "Usage: entrypoint.sh {db-init|realmd|mangosd}" >&2
    exit 1
    ;;
esac
