#!/usr/bin/env bash
# End-to-end test of cRPC against a fake Continuum client and a fake Discord
# client, using MinGW + Wine (+ Xvfb when there is no display). Exercises
# process tracking, memory reading and the Discord IPC connection.
#
#   tests/integration/run.sh [work-dir]
#
# Requires: i686-w64-mingw32-g++, wine (32-bit), xvfb-run (headless only).
set -euo pipefail

# Every Wine process must share one display or window focus will not work
if [ -z "${DISPLAY:-}" ]; then exec xvfb-run -a "$0" "$@"; fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${1:-$(mktemp -d)}"
mkdir -p "$WORK/game"
CXX="i686-w64-mingw32-g++ -std=c++17 -O2 -static"

echo "== building into $WORK/game"
i686-w64-mingw32-windres "$ROOT/DiscordBridge.rc" -O coff -o "$WORK/res.o"
$CXX -municode -mwindows -DUNICODE -D_UNICODE -I"$ROOT" "$ROOT"/*.cpp "$WORK/res.o" -o "$WORK/game/Continuum.exe"
$CXX -shared "$ROOT/tests/integration/fake_menu040.cpp" -o "$WORK/game/menu040.dll"
$CXX "$ROOT/tests/integration/fake_continuum.cpp" -o "$WORK/game/Continuum_main.exe"
$CXX "$ROOT/tests/integration/fake_discord.cpp" -o "$WORK/fake_discord.exe"

RUN=(wine)
export WINEDEBUG=-all WINEPREFIX="${WINEPREFIX:-$WORK/prefix}"

cd "$WORK/game"
rm -f cRPC.log "$WORK/discord.log"
OVERRIDES="$("${RUN[@]}" ./Continuum_main.exe --print-rva | tr -d '\r')"
cat > cRPC.ini <<EOF
[General]
Debug=1
StartupTimeoutSec=20
[Memory]
$OVERRIDES
[Presence]
ShowPlayerName=1
StatusDisplay=2
EOF
echo "== overrides:"; echo "$OVERRIDES"

echo "== starting fake Discord"
wine "$WORK/fake_discord.exe" 'Z:'"$(echo "$WORK/discord.log" | tr / '\\')" >/dev/null 2>&1 &
DISCORD_PID=$!
for _ in $(seq 50); do grep -q LISTENING "$WORK/discord.log" 2>/dev/null && break; sleep 0.2; done

echo "== running cRPC"
"${RUN[@]}" ./Continuum.exe || true
wait "$DISCORD_PID" || true

tr -d '\r' < cRPC.log > "$WORK/cRPC.log"
echo "== cRPC.log"; cat "$WORK/cRPC.log"
echo "== discord.log"; cat "$WORK/discord.log"

fail=0
expect() { # expect <file> <text>
	if grep -qF -- "$2" "$WORK/$1"; then echo "PASS: $1: $2"; else echo "FAIL: $1: $2"; fail=1; fi
}
# game tracking and memory reading
expect cRPC.log "launch: started"
expect cRPC.log "game: tracking pid"
expect cRPC.log "screen=launcher focused=1"
expect cRPC.log "screen=zone focused=1 mem(readable=1 loaded=1 self=1) zone='Trench Wars' map='trench.lvl' name='Purge' ship=1 freq=1 bounty=42 flags=0 safe=0 players=4 playing=3 team=2"
expect cRPC.log "ship=7 freq=1 bounty=120 flags=2 safe=1 players=4 playing=3 team=2"
# Discord connection
expect cRPC.log "discord: connected"
expect cRPC.log "SET_ACTIVITY rejected"
expect discord.log 'HANDSHAKE {"v":1,"client_id":"611578814073405465"}'
expect discord.log 'PONG {"ping":1}'
expect discord.log '"details":"Selecting Zone"'
expect discord.log '"details":"Trench Wars","state":"Javelin · Freq 1 · Bounty 42"'
expect discord.log '"party":{"id":"crpc:Trench Wars:trench.lvl:1","size":[2,4]}'
expect discord.log '"small_image":"sha","small_text":"Purge in a Shark"'
expect discord.log '"state":"Shark · Freq 1 · Bounty 120 · 2 flags · In Safe"'
expect discord.log '"status_display_type":2'
expect discord.log '"buttons":[{"label":"Play Subspace Continuum","url":"https://store.steampowered.com/app/352700/Subspace_Continuum/"}]'
expect discord.log 'CLEAR {"cmd":"SET_ACTIVITY","args":{"pid":'
expect discord.log "CLIENT CLOSED"
expect cRPC.log "exiting"
expect cRPC.log "alive: game pid"
if [ -e cRPC-crash.log ]; then echo "FAIL: cRPC-crash.log exists"; cat cRPC-crash.log; fail=1; fi

# Every way cRPC can die must leave a trace in cRPC-crash.log
for kind in access abort throw; do
	rm -f cRPC-crash.log
	CRPC_TEST_CRASH=$kind "${RUN[@]}" ./Continuum.exe >/dev/null 2>&1 || true
	tr -d '\r' < cRPC-crash.log > "$WORK/crash-$kind.log" 2>/dev/null || true
	echo "== crash test '$kind':"; cat "$WORK/crash-$kind.log"
done
expect crash-access.log "crashed: exception 0xC0000005"
expect crash-access.log "Continuum.exe + 0x"
expect crash-abort.log "abort() was called"
expect crash-throw.log "std::terminate: an exception was not handled"
exit $fail
