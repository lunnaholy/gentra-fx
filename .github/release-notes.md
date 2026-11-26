## Установка

**macOS (Apple Silicon)** — распакуйте архив и скопируйте:
- `Sauce Gentra FX.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
- `Sauce Gentra FX.component` → `~/Library/Audio/Plug-Ins/Components/`

Сборка пока не подписана Apple, поэтому macOS её блокирует. Снимите блокировку в Терминале и перезапустите DAW:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"Sauce Gentra FX.vst3" ~/Library/Audio/Plug-Ins/Components/"Sauce Gentra FX.component"
```

**Windows (64-bit)** — распакуйте архив и скопируйте папку `Sauce Gentra FX.vst3` в `C:\Program Files\Common Files\VST3\`.

**Linux (x64)** — распакуйте архив и скопируйте папку `Sauce Gentra FX.vst3` в `~/.vst3/`.
