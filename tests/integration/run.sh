#!/usr/bin/env bash
# End-to-end test of cRPC's process tracking and memory reader against a fake
# Continuum client, using MinGW + Wine (+ Xvfb when there is no display).
#
#   tests/integration/run.sh [work-dir]
#
# Requires: i686-w64-mingw32-g++, wine (32-bit), xvfb-run (headless only).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="${1:-$(mktemp -d)}"
mkdir -p "$WORK/game" "$WORK/shim"
CXX="i686-w64-mingw32-g++ -std=c++17 -O2 -static"

# The Discord SDK headers use <Windows.h>; MinGW on Linux is case sensitive
MINGW_INC="$(dirname "$(echo '#include <windows.h>' | i686-w64-mingw32-g++ -xc++ -E -H - 2>&1 >/dev/null | grep -m1 'windows.h' | sed 's/^\.* //')")"
ln -sf "$MINGW_INC/windows.h" "$WORK/shim/Windows.h"

echo "== building into $WORK/game"
$CXX -municode -mwindows -DUNICODE -D_UNICODE -include cstdint -I"$WORK/shim" -I"$ROOT" \
	"$ROOT"/Activity.cpp "$ROOT"/Config.cpp "$ROOT"/Continuum.cpp "$ROOT"/DiscordBridge.cpp \
	"$ROOT"/GameMemory.cpp "$ROOT"/Presence.cpp "$ROOT"/discordsdk/*.cpp \
	"$ROOT"/binaries/Win32/discord_game_sdk.dll -o "$WORK/game/Continuum.exe"
$CXX -shared "$ROOT/tests/integration/fake_menu040.cpp" -o "$WORK/game/menu040.dll"
$CXX "$ROOT/tests/integration/fake_continuum.cpp" -o "$WORK/game/Continuum_main.exe"
cp "$ROOT/binaries/Win32/discord_game_sdk.dll" "$WORK/game/"

RUN=(wine)
if [ -z "${DISPLAY:-}" ]; then RUN=(xvfb-run -a wine); fi
export WINEDEBUG=-all WINEPREFIX="${WINEPREFIX:-$WORK/prefix}"

cd "$WORK/game"
OVERRIDES="$("${RUN[@]}" ./Continuum_main.exe --print-rva | tr -d '\r')"
cat > cRPC.ini <<EOF
[General]
Debug=1
StartupTimeoutSec=20
[Memory]
$OVERRIDES
[Presence]
ShowPlayerName=1
EOF
echo "== overrides:"; echo "$OVERRIDES"

echo "== running cRPC"
"${RUN[@]}" ./Continuum.exe || true
echo "== cRPC.log"
tr -d '\r' < cRPC.log | tee "$WORK/cRPC.log"

fail=0
expect() {
	if grep -qF -- "$1" "$WORK/cRPC.log"; then echo "PASS: $1"; else echo "FAIL: $1"; fail=1; fi
}
expect "launch: started"
expect "game: tracking pid"
expect "screen=launcher focused=1"
expect "name='Purge'"
expect "screen=zone focused=1 mem(readable=1 loaded=1 self=1) zone='Trench Wars' map='trench.lvl' name='Purge' ship=1 freq=1 bounty=42 flags=0 safe=0 players=4 playing=3 team=2"
expect "ship=7 freq=1 bounty=120 flags=2 safe=1 players=4 playing=3 team=2"
expect "exiting"
exit $fail
