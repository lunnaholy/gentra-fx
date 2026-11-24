#!/usr/bin/env bash
# Ставит собранные VST3 и AU в ~/Library/Audio/Plug-Ins/ — оттуда их берёт DAW (macOS).
#   ./install.sh        — поставить то, что уже собрано
#   ./install.sh build  — сначала собрать (./build.sh), потом поставить
# DAW держит загруженный плагин в памяти: после установки перезапустите DAW
# (или удалите плагин из проекта и пересканируйте), иначе играет старая версия.
set -euo pipefail
cd "$(dirname "$0")"

case "${1:-}" in
    build) ./build.sh ;;
    "") ;;
    *) echo "Неизвестный аргумент: $1. Использование: ./install.sh [build]" >&2; exit 1 ;;
esac

artefacts="build/SauceGentraFX_artefacts/Release"
plugins="$HOME/Library/Audio/Plug-Ins"
name="Sauce Gentra FX"

# install <папка в артефактах> <папка в Plug-Ins> <расширение>
install() {
    local src="$artefacts/$1/$name.$3" dst="$plugins/$2/$name.$3"
    if [ ! -d "$src" ]; then
        echo "Нет $src — сначала соберите: ./install.sh build" >&2
        exit 1
    fi
    mkdir -p "$plugins/$2"
    rm -rf "$dst" # целиком: от старой версии не должно остаться лишних файлов
    ditto "$src" "$dst"
    echo "✓ $dst (собран $(stat -f '%Sm' "$src/Contents/MacOS/$name"))"
}

install VST3 VST3 vst3
install AU Components component

# Сброс кэша Audio Units, чтобы Logic/FL увидели новую версию AU.
killall -9 AudioComponentRegistrar 2>/dev/null || true

echo "Готово. Перезапустите DAW, чтобы подхватить новую версию."
