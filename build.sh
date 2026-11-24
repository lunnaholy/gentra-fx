#!/usr/bin/env bash
# Быстрая сборка (macOS): ./build.sh [clean] [run]
#   clean — удалить build/ и собрать с нуля
#   run   — после сборки открыть Standalone
# VST3 и AU после сборки сами копируются в ~/Library/Audio/Plug-Ins/.
set -euo pipefail
cd "$(dirname "$0")"

for arg in "$@"; do
    case "$arg" in
        clean|run) ;;
        *) echo "Неизвестный аргумент: $arg. Использование: ./build.sh [clean] [run]" >&2; exit 1 ;;
    esac
done

[[ " $* " == *" clean "* ]] && rm -rf build

[ -f build/CMakeCache.txt ] || cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure

if [[ " $* " == *" run "* ]]; then
    pkill -f "Sauce Gentra FX.app/Contents/MacOS" || true # старое окно показало бы старую версию
    open "build/SauceGentraFX_artefacts/Release/Standalone/Sauce Gentra FX.app"
fi
