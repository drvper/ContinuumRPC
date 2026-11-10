#!/usr/bin/env bash
# End-to-end test of cRPC against a fake Continuum client and a fake Discord
# client, using MinGW + Wine + Xvfb. Exercises process tracking, memory
# reading, the window/registry/keyboard fallback for a client that refuses
# access, the Discord IPC connection and the crash diagnostics.
#
#   tests/integration/run.sh [work-dir]
#
# Requires: i686-w64-mingw32-g++, wine (32-bit), xvfb-run (headless only).
set -euo pipefail

# Every Wine process must share one display or window focus will not work
if [ -z "${DISPLAY:-}" ]; then exec xvfb-run -a "$0" "$@"; fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${1:-$(mktemp -d)}"
GAME="$WORK/game"
mkdir -p "$GAME"
CXX="i686-w64-mingw32-g++ -std=c++17 -O2 -static"

echo "== building into $GAME"
i686-w64-mingw32-windres "$ROOT/DiscordBridge.rc" -O coff -o "$WORK/res.o"
$CXX -municode -mwindows -DUNICODE -D_UNICODE -I"$ROOT" "$ROOT"/*.cpp "$WORK/res.o" -o "$GAME/Continuum.exe"
$CXX -shared "$ROOT/tests/integration/fake_menu040.cpp" -o "$GAME/menu040.dll"
$CXX "$ROOT/tests/integration/fake_continuum.cpp" -o "$GAME/Continuum_main.exe"
$CXX "$ROOT/tests/integration/fake_discord.cpp" -o "$WORK/fake_discord.exe"

export WINEDEBUG=-all WINEPREFIX="${WINEPREFIX:-$WORK/prefix}"
cd "$GAME"
OVERRIDES="$(wine ./Continuum_main.exe --print-rva | tr -d '\r')"
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

# run_scenario <name> [VAR=value ...]: one game session with a fresh fake Discord
run_scenario() {
	local name="$1"; shift
	rm -f cRPC.log cRPC-crash.log "$WORK/$name-discord.log"
	wine "$WORK/fake_discord.exe" 'Z:'"$(echo "$WORK/$name-discord.log" | tr / '\\')" >/dev/null 2>&1 &
	local discord=$!
	for _ in $(seq 50); do grep -q LISTENING "$WORK/$name-discord.log" 2>/dev/null && break; sleep 0.2; done
	env "$@" wine ./Continuum.exe >/dev/null 2>&1 || true
	wait "$discord" || true
	tr -d '\r' < cRPC.log > "$WORK/$name-cRPC.log"
	[ -e cRPC-crash.log ] && cp cRPC-crash.log "$WORK/$name-crash.log"
	echo "== $name: cRPC.log"; cat "$WORK/$name-cRPC.log"
	echo "== $name: discord.log"; cat "$WORK/$name-discord.log"
}

fail=0
expect() { # expect <file> <text>
	if grep -qF -- "$2" "$WORK/$1"; then echo "PASS: $1: $2"; else echo "FAIL: $1: $2"; fail=1; fi
}
expect_no_file() {
	if [ -e "$WORK/$1" ]; then echo "FAIL: $1 exists"; cat "$WORK/$1"; fail=1; else echo "PASS: no $1"; fi
}

# 1. Normal client: everything comes from memory
run_scenario normal FAKE_NORMAL=1
expect normal-cRPC.log "launch: started"
expect normal-cRPC.log "game: tracking pid"
expect normal-cRPC.log "screen=launcher focused=1"
expect normal-cRPC.log "screen=zone focused=1 mem(readable=1 loaded=1 self=1) zone='Trench Wars' map='trench.lvl' name='Purge' ship=1 freq=1 bounty=42 flags=0 safe=0 players=4 playing=3 team=2"
expect normal-cRPC.log "ship=7 freq=1 bounty=120 flags=2 safe=1 players=4 playing=3 team=2"
expect normal-cRPC.log "discord: connected"
expect normal-cRPC.log "SET_ACTIVITY rejected"
expect normal-cRPC.log "alive: game pid"
expect normal-cRPC.log "exiting: the game was closed"
expect normal-discord.log 'HANDSHAKE {"v":1,"client_id":"611578814073405465"}'
expect normal-discord.log 'PONG {"ping":1}'
expect normal-discord.log '"details":"Selecting Zone"'
expect normal-discord.log '"details":"Trench Wars","state":"Javelin · Freq 1 · Bounty 42"'
expect normal-discord.log '"party":{"id":"crpc:Trench Wars:trench.lvl:1","size":[2,4]}'
expect normal-discord.log '"small_image":"sha","small_text":"Purge in a Shark"'
expect normal-discord.log '"state":"Shark · Freq 1 · Bounty 120 · 2 flags · In Safe"'
expect normal-discord.log '"status_display_type":2'
expect normal-discord.log '"buttons":[{"label":"Play Subspace Continuum","url":"https://store.steampowered.com/app/352700/Subspace_Continuum/"}]'
expect normal-discord.log 'CLEAR {"cmd":"SET_ACTIVITY","args":{"pid":'
expect normal-discord.log "CLIENT CLOSED"
expect_no_file normal-crash.log

# 2. Client that refuses to be opened (as reported on a real install): no
#    handle at all, no memory; window titles, registry and the ship keys
run_scenario protected FAKE_PROTECT=1 CRPC_TEST_NO_PROCESS_HANDLE=1
expect protected-cRPC.log "through the process list (the game refused access"
expect protected-cRPC.log "game: access to pid"
expect protected-cRPC.log "(access denied by the game)"
expect protected-cRPC.log "memory: not available, using window titles, the registry and the ship-select keys instead"
expect protected-cRPC.log "screen=zone focused=1 mem(readable=0 loaded=0 self=0) zone='Trench Wars'"
expect protected-cRPC.log "keys: ship changed to Shark"
expect protected-cRPC.log "exiting: the game was closed"
expect protected-discord.log '"details":"Trench Wars","state":"Javelin"'
expect protected-discord.log '"details":"Trench Wars","state":"Shark"'
expect protected-discord.log '"small_image":"sha","small_text":"Playing as Shark"'
expect protected-discord.log "CLIENT CLOSED"
expect_no_file protected-crash.log

# 3. Every way cRPC can die must leave a trace in cRPC-crash.log
for kind in access abort throw; do
	rm -f cRPC-crash.log
	CRPC_TEST_CRASH=$kind wine ./Continuum.exe >/dev/null 2>&1 || true
	tr -d '\r' < cRPC-crash.log > "$WORK/crash-$kind.log" 2>/dev/null || true
	echo "== crash test '$kind':"; cat "$WORK/crash-$kind.log"
done
expect crash-access.log "crashed: exception 0xC0000005"
expect crash-access.log "Continuum.exe + 0x"
expect crash-abort.log "abort() was called"
expect crash-throw.log "std::terminate: an exception was not handled"
exit $fail
