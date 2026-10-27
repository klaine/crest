#!/bin/sh
# Follow Schwung's debug log on a Move.   scripts/logs.sh [host]
# The log path is the documented default; adjust it if your Schwung version differs.
# -4: move.local also advertises IPv6 addresses, and ssh fails trying those first.
HOST=${1:-${MOVE_HOST:-move.local}}
exec ssh -4 "${MOVE_USER:+$MOVE_USER@}$HOST" "tail -n 100 -f /data/UserData/schwung/debug.log"
