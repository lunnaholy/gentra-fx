// Самопроверка настоящего процессора без хоста. Падает с ненулевым кодом, если что-то сломано.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <juce_audio_utils/juce_audio_utils.h>

#include "../Source/PluginEditor.h"

#define CHECK(cond) \
    do { if (! (cond)) { std::printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); std::exit (1); } } while (false)

namespace
{
// value — в единицах параметра (%, Гц, бит, кГц; для вкл/выкл — 0 или 1).
void setParam (GentraAudioProcessor& p, const char* id, float value)
{
    auto* param = p.apvts.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}

void fillNoise (juce::AudioBuffer<float>& b, juce::Random& rng, float gain)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int n = 0; n < b.getNumSamples(); ++n)
            b.setSample (ch, n, gain * (rng.nextFloat() * 2.0f - 1.0f));
}

bool bitExact (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * static_cast<size_t> (a.getNumSamples())) != 0)
            return false;
    return true;
}

double rms (const juce::AudioBuffer<float>& b)
{
    double sum = 0.0;
    for (int n = 0; n < b.getNumSamples(); ++n)
        sum += static_cast<double> (b.getSample (0, n)) * static_cast<double> (b.getSample (0, n));
    return std::sqrt (sum / b.getNumSamples());
}

// Прогоняет 0.2 с шума, чтобы сглаживание доехало.
void settle (GentraAudioProcessor& p, double sampleRate, juce::Random& rng)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, static_cast<int> (sampleRate * 0.2));
    fillNoise (buf, rng, 0.5f);
    p.processBlock (buf, midi);
}

bool transparent (GentraAudioProcessor& p, juce::Random& rng)
{
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf (2, 512), ref;
    fillNoise (buf, rng, 1.0f);
    ref.makeCopyOf (buf);
    p.processBlock (buf, midi);
    return bitExact (buf, ref);
}

// Амплитуда частоты freq в канале 0 (корреляция с синусом и косинусом).
double amplitudeAt (const juce::AudioBuffer<float>& b, double freq, double sampleRate)
{
    double re = 0.0, im = 0.0;
    for (int n = 0; n < b.getNumSamples(); ++n)
    {
        const double w = juce::MathConstants<double>::twoPi * freq * n / sampleRate;
        re += b.getSample (0, n) * std::cos (w);
        im += b.getSample (0, n) * std::sin (w);
    }
    return 2.0 * std::sqrt (re * re + im * im) / b.getNumSamples();
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int n = 0; n < b.getNumSamples(); ++n)
            if (! std::isfinite (b.getSample (ch, n)))
                return false;
    return true;
}

void run (double sampleRate)
{
    GentraAudioProcessor p;
    juce::MidiBuffer midi;
    juce::Random rng (42);
    p.setRateAndBufferSizeDetails (sampleRate, 512);

    // 1. Обе ручки на 0 — побитово прозрачно при любых размерах блока.
    p.prepareToPlay (sampleRate, 512);
    for (int size : { 1, 7, 64, 512, 333 })
    {
        juce::AudioBuffer<float> buf (2, size), ref;
        fillNoise (buf, rng, 1.5f);
        ref.makeCopyOf (buf);
        p.processBlock (buf, midi);
        CHECK (bitExact (buf, ref));
    }

    // 2. Bass 100%: тихий синус 50 Hz заметно громче, full-scale — не выше 1.0.
    setParam (p, "bass", 100.0f);
    p.prepareToPlay (sampleRate, 512);
    for (float amp : { 0.1f, 1.0f })
    {
        p.reset();
        juce::AudioBuffer<float> buf (2, static_cast<int> (sampleRate)); // 1 секунда
        for (int n = 0; n < buf.getNumSamples(); ++n)
        {
            const auto s = amp * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 50.0 * n / sampleRate));
            buf.setSample (0, n, s);
            buf.setSample (1, n, s);
        }
        p.processBlock (buf, midi);
        const float peak = buf.getMagnitude (0, buf.getNumSamples());
        CHECK (allFinite (buf));
        CHECK (peak <= 1.0f);
        if (amp < 0.5f)
            CHECK (peak > amp * 2.5f); // шельф на 50 Hz даёт около +11 dB
    }

    // 3. Lo-Res 100%: сигнал «ступеньками», значения на сетке 4 бит.
    setParam (p, "bass", 0.0f);
    setParam (p, "lores", 100.0f);
    p.prepareToPlay (sampleRate, 512);
    {
        juce::AudioBuffer<float> buf (2, 4096);
        fillNoise (buf, rng, 0.9f);
        p.processBlock (buf, midi);
        int changes = 0;
        for (int n = 1; n < buf.getNumSamples(); ++n)
        {
            changes += std::abs (buf.getSample (0, n) - buf.getSample (0, n - 1)) > 0.0f ? 1 : 0;
            const float scaled = buf.getSample (0, n) * 8.0f;
            CHECK (std::abs (scaled - std::round (scaled)) < 1.0e-6f);
        }
        const int expected = static_cast<int> (4096.0 * 1500.0 / sampleRate);
        CHECK (changes > 0 && changes <= expected + 2);
    }

    // 4. Все на 100%: тишина остаётся тишиной.
    setParam (p, "bass", 100.0f);
    setParam (p, "drive", 100.0f);
    p.prepareToPlay (sampleRate, 512);
    {
        juce::AudioBuffer<float> buf (2, 2048);
        buf.clear();
        p.processBlock (buf, midi);
        CHECK (buf.getMagnitude (0, buf.getNumSamples()) <= 0.0f);
    }

    // 5. Случайная автоматизация и размеры блока: без NaN/Inf, пик ограничен.
    for (int i = 0; i < 300; ++i)
    {
        setParam (p, "bass", rng.nextFloat() * 100.0f);
        setParam (p, "lores", rng.nextFloat() * 100.0f);
        setParam (p, "drive", rng.nextFloat() * 100.0f);
        juce::AudioBuffer<float> buf (2, 1 + rng.nextInt (700));
        fillNoise (buf, rng, 1.0f);
        p.processBlock (buf, midi);
        CHECK (allFinite (buf));
        CHECK (buf.getMagnitude (0, buf.getNumSamples()) < 8.0f);
    }

    // 6. Вернули ручки в 0 — после сглаживания снова побитово прозрачно.
    setParam (p, "bass", 0.0f);
    setParam (p, "lores", 0.0f);
    setParam (p, "drive", 0.0f);
    {
        juce::AudioBuffer<float> settle (2, static_cast<int> (sampleRate * 0.2));
        fillNoise (settle, rng, 0.5f);
        p.processBlock (settle, midi);

        juce::AudioBuffer<float> buf (2, 512), ref;
        fillNoise (buf, rng, 1.0f);
        ref.makeCopyOf (buf);
        p.processBlock (buf, midi);
        CHECK (bitExact (buf, ref));
    }

    // 7. Ручка выкручена, но Mix = 0 или машина выключена — побитово прозрачно.
    setParam (p, "bass", 100.0f);
    setParam (p, "lores", 100.0f);
    setParam (p, "bassMix", 0.0f);
    setParam (p, "loresOn", 0.0f);
    setParam (p, "drive", 100.0f);
    setParam (p, "distOn", 0.0f);
    settle (p, sampleRate, rng);
    CHECK (transparent (p, rng));
    setParam (p, "bassMix", 100.0f);
    setParam (p, "loresOn", 1.0f);
    setParam (p, "distOn", 1.0f);
    setParam (p, "bass", 0.0f);
    setParam (p, "lores", 0.0f);
    setParam (p, "drive", 0.0f);

    // 8. Тонировка глушит верх даже при нулевом басе, а вернувшись в 0 — снова прозрачно.
    {
        juce::AudioBuffer<float> hiss (2, 8192);
        for (int n = 0; n < hiss.getNumSamples(); ++n) // чередование ±0.5 — чистая частота Найквиста
            for (int ch = 0; ch < 2; ++ch)
                hiss.setSample (ch, n, (n % 2 == 0) ? 0.5f : -0.5f);
        setParam (p, "tint", 100.0f);
        settle (p, sampleRate, rng);
        p.processBlock (hiss, midi);
        CHECK (rms (hiss) < 0.05);
        setParam (p, "tint", 0.0f);
        settle (p, sampleRate, rng);
        CHECK (transparent (p, rng));
    }

    // 9. Драйв 100%, Mix 100%, тон открыт: синус становится почти меандром (3-я гармоника ~1/3), громкость примерно та же.
    {
        setParam (p, "drive", 100.0f);
        setParam (p, "distMix", 100.0f);
        setParam (p, "distTone", 100.0f);
        settle (p, sampleRate, rng);
        juce::AudioBuffer<float> buf (2, static_cast<int> (sampleRate * 0.5));
        for (int n = 0; n < buf.getNumSamples(); ++n)
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, n, 0.25f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 220.0 * n / sampleRate)));
        p.processBlock (buf, midi);
        CHECK (allFinite (buf));
        const double level = rms (buf), inRms = 0.25 / std::sqrt (2.0);
        const double third = amplitudeAt (buf, 660.0, sampleRate) / amplitudeAt (buf, 220.0, sampleRate);
        std::printf ("drive %.0f Hz: 3rd harmonic %.2f (square 0.33), level %+.1f dB\n", sampleRate, third, 20.0 * std::log10 (level / inRms));
        CHECK (third > 0.25);
        CHECK (std::abs (20.0 * std::log10 (level / inRms)) < 4.0);

        setParam (p, "drive", 0.0f);
        setParam (p, "distMix", 45.0f);
        setParam (p, "distTone", 50.0f);
        settle (p, sampleRate, rng);
        CHECK (transparent (p, rng));
    }

    // 10. Метр низа: бочка 50 Гц видна, тишина — ноль.
    {
        p.takeBassLevel();
        juce::AudioBuffer<float> buf (2, 4096);
        for (int n = 0; n < buf.getNumSamples(); ++n)
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, n, 0.8f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 50.0 * n / sampleRate)));
        p.processBlock (buf, midi);
        CHECK (p.takeBassLevel() > 0.5f);
        buf.clear();
        p.reset();
        p.processBlock (buf, midi);
        CHECK (p.takeBassLevel() <= 0.0f);
    }

    // 11. Состояние переживает сохранение/загрузку.
    setParam (p, "bass", 37.0f);
    setParam (p, "lores", 82.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    GentraAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (std::abs (q.apvts.getRawParameterValue ("bass")->load() - 37.0f) < 0.01f);
    CHECK (std::abs (q.apvts.getRawParameterValue ("lores")->load() - 82.0f) < 0.01f);
    CHECK (std::abs (q.apvts.getRawParameterValue ("tint")->load()) < 0.01f);
}

// Хост с заданной позицией и темпом.
struct FakeHead final : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

// Белая: громкость и шельфы дают заявленное усиление, в нуле / Mix 0 / выкл — побитово прозрачно.
void equalizer (double sampleRate)
{
    GentraAudioProcessor p;
    p.setRateAndBufferSizeDetails (sampleRate, 512);
    p.prepareToPlay (sampleRate, 512);
    juce::Random rng (7);
    juce::MidiBuffer midi;

    // Усиление синуса freq в дБ при текущих настройках.
    const auto gainAt = [&] (double freq)
    {
        settle (p, sampleRate, rng);
        juce::AudioBuffer<float> buf (2, static_cast<int> (sampleRate * 0.5));
        for (int n = 0; n < buf.getNumSamples(); ++n)
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, n, 0.1f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * freq * n / sampleRate)));
        p.processBlock (buf, midi);
        CHECK (allFinite (buf));
        return 20.0 * std::log10 (amplitudeAt (buf, freq, sampleRate) / 0.1);
    };

    setParam (p, "eqVolume", 6.0f);
    CHECK (std::abs (gainAt (1000.0) - 6.0) < 0.1);
    setParam (p, "eqVolume", 0.0f);
    setParam (p, "eqLow", 12.0f);
    const double lowBoost = gainAt (50.0), lowLeavesTop = gainAt (10000.0);
    setParam (p, "eqLow", 0.0f);
    setParam (p, "eqHigh", 12.0f);
    const double highBoost = gainAt (10000.0), highLeavesBottom = gainAt (50.0);
    setParam (p, "eqLow", -12.0f);
    const double lowCut = gainAt (50.0);
    setParam (p, "eqLow", 0.0f);
    setParam (p, "eqHigh", 0.0f);
    setParam (p, "eqMid", 12.0f);
    const double midBoost = gainAt (1000.0), midLeavesBottom = gainAt (50.0), midLeavesTop = gainAt (15000.0);
    std::printf ("eq %.0f Hz: mid +12 -> 1 kHz %+.1f dB, 50 Hz %+.1f dB, 15 kHz %+.1f dB\n", sampleRate, midBoost, midLeavesBottom, midLeavesTop);
    CHECK (midBoost > 11.5 && std::abs (midLeavesBottom) < 0.5 && std::abs (midLeavesTop) < 0.5);
    setParam (p, "eqMid", 0.0f);
    std::printf ("eq %.0f Hz: low +12 -> 50 Hz %+.1f dB, 10 kHz %+.1f dB; high +12 -> 10 kHz %+.1f dB, 50 Hz %+.1f dB; low -12 -> %+.1f dB\n",
                 sampleRate, lowBoost, lowLeavesTop, highBoost, highLeavesBottom, lowCut);
    CHECK (lowBoost > 10.5 && std::abs (lowLeavesTop) < 0.3);
    CHECK (highBoost > 10.5 && std::abs (highLeavesBottom) < 0.3);
    CHECK (lowCut < -10.5);

    setParam (p, "eqMix", 0.0f);
    settle (p, sampleRate, rng);
    CHECK (transparent (p, rng));
    setParam (p, "eqMix", 100.0f);
    setParam (p, "eqOn", 0.0f);
    settle (p, sampleRate, rng);
    CHECK (transparent (p, rng));
    setParam (p, "eqOn", 1.0f);
    settle (p, sampleRate, rng);
    CHECK (transparent (p, rng));
}

// Серебряная: повтор ровно через заданное время, обратная связь, смена времени без щелчков и изломов,
// доля такта по темпу хоста, хвост для хоста; выключенная — побитово прозрачна.
void delayCar (double sampleRate)
{
    GentraAudioProcessor p;
    p.setRateAndBufferSizeDetails (sampleRate, 512);
    p.prepareToPlay (sampleRate, 512);
    juce::MidiBuffer midi;
    CHECK (p.getTailLengthSeconds() <= 0.0); // по умолчанию выключен

    setParam (p, "delayOn", 1.0f);
    setParam (p, "delayMix", 100.0f);
    setParam (p, "delayFeedback", 0.0f);
    setParam (p, "delayTone", 100.0f);

    // Тишина seconds секунд (сглаживание доезжает, линия пустеет), затем импульс; возвращает отклик.
    const auto impulse = [&] (double seconds)
    {
        juce::AudioBuffer<float> buf (2, static_cast<int> (sampleRate * 1.5));
        buf.clear();
        p.processBlock (buf, midi);
        juce::AudioBuffer<float> out (2, static_cast<int> (sampleRate * seconds));
        out.clear();
        out.setSample (0, 0, 1.0f);
        out.setSample (1, 0, 1.0f);
        p.processBlock (out, midi);
        CHECK (allFinite (out));
        return out;
    };

    // По умолчанию — по нотам, 1/8; без хоста темп 120 — повтор через 0.25 с.
    {
        CHECK (p.apvts.getParameter ("delayNote")->getCurrentValueAsText() == "1/8");
        const auto out = impulse (0.4);
        CHECK (std::abs (out.getSample (0, juce::roundToInt (0.25 * sampleRate)) - 1.0f) < 1.0e-6f);
    }

    // В миллисекундах, без обратной связи: один повтор ровно через 100 мс, больше ничего (Mix 100% — сухого нет).
    setParam (p, "delaySync", 0.0f);
    setParam (p, "delayTime", 100.0f);
    {
        const auto out = impulse (0.3);
        const int at = juce::roundToInt (0.1 * sampleRate);
        CHECK (std::abs (out.getSample (0, at) - 1.0f) < 1.0e-6f);
        float rest = 0.0f;
        for (int n = 0; n < out.getNumSamples(); ++n)
            if (n != at)
                rest = juce::jmax (rest, std::abs (out.getSample (0, n)));
        CHECK (rest < 1.0e-6f);
    }

    // Повторы 50%: второй повтор через 200 мс несёт половину энергии первого (тон открыт — фильтр её не съедает).
    setParam (p, "delayFeedback", 50.0f);
    {
        const auto out = impulse (0.5);
        const int second = juce::roundToInt (0.2 * sampleRate);
        float sum = 0.0f;
        for (int n = second - 20; n < second + static_cast<int> (0.01 * sampleRate); ++n)
            sum += out.getSample (0, n);
        CHECK (std::abs (sum - 0.5f) < 0.02f);
        CHECK (p.getTailLengthSeconds() > 1.0); // 0.1 с × ~11 повторов до −60 дБ
    }

    // Смена времени 100 → 400 мс на синусе: вторая разность выхода как у гладкого синуса (с поправкой на высоту
    // при езде головки), а скачок головки или излом скорости дали бы 0.14 и больше.
    {
        setParam (p, "delayFeedback", 0.0f);
        juce::AudioBuffer<float> buf (2, 64);
        long long t = 0;
        float y1 = 0.0f, y2 = 0.0f, worst = 0.0f;
        for (int b = 0; b < static_cast<int> (1.5 * sampleRate / 64.0); ++b)
        {
            if (b == static_cast<int> (0.5 * sampleRate / 64.0))
                setParam (p, "delayTime", 400.0f);
            for (int n = 0; n < 64; ++n)
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, n, 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 1000.0 * static_cast<double> (t + n) / sampleRate)));
            t += 64;
            p.processBlock (buf, midi);
            for (int n = 0; n < 64; ++n)
            {
                const float y = buf.getSample (0, n);
                if (b * 64 + n > static_cast<int> (0.3 * sampleRate))
                    worst = juce::jmax (worst, std::abs (y - 2.0f * y1 + y2));
                y2 = y1;
                y1 = y;
            }
        }
        const double smooth = 0.5 * std::pow (juce::MathConstants<double>::twoPi * 1000.0 / sampleRate, 2.0);
        std::printf ("delay %.0f Hz: time change 2nd diff %.4f (still sine %.4f)\n", sampleRate, worst, smooth);
        CHECK (worst < 3.0 * smooth);
    }

    // По нотам с темпом хоста: 90 BPM, нота 1/4 — повтор через 60/90 с; хост видит «1/4».
    // Самая короткая — 1/128: при 90 BPM повтор через 60/90/32 с.
    {
        FakeHead head;
        head.info.setBpm (90.0); // транспорт стоит, но темп хост сообщает
        p.setPlayHead (&head);
        setParam (p, "delaySync", 1.0f);
        setParam (p, "delayNote", 15.0f); // 1/4
        CHECK (p.apvts.getParameter ("delayNote")->getCurrentValueAsText() == "1/4");
        const auto out = impulse (0.8);
        CHECK (std::abs (out.getSample (0, juce::roundToInt (60.0 / 90.0 * sampleRate)) - 1.0f) < 1.0e-6f);
        setParam (p, "delayNote", 0.0f);
        CHECK (p.apvts.getParameter ("delayNote")->getCurrentValueAsText() == "1/128");
        const auto shortest = impulse (0.1);
        CHECK (std::abs (shortest.getSample (0, juce::roundToInt (60.0 / 90.0 / 32.0 * sampleRate)) - 1.0f) < 1.0e-6f);
        p.setPlayHead (nullptr);
    }

    juce::Random rng (3);
    setParam (p, "delayOn", 0.0f);
    settle (p, sampleRate, rng);
    CHECK (transparent (p, rng));
    CHECK (p.getTailLengthSeconds() <= 0.0);
}

// Случайная автоматизация белой и серебряной (вместе с остальными) и размеры блока: без NaN/Inf и разноса.
void automation (double sampleRate)
{
    GentraAudioProcessor p;
    p.setRateAndBufferSizeDetails (sampleRate, 512);
    p.prepareToPlay (sampleRate, 512);
    juce::Random rng (11);
    juce::MidiBuffer midi;
    const auto any = [&] (const char* id) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (rng.nextFloat()); };
    for (int i = 0; i < 300; ++i)
    {
        for (auto id : { "eqVolume", "eqLow", "eqMid", "eqHigh", "eqMix", "delayTime", "delayNote", "delayFeedback", "delayTone", "delayMix", "drive", "bass" })
            any (id);
        setParam (p, "delayOn", rng.nextBool() ? 1.0f : 0.0f);
        setParam (p, "delaySync", rng.nextBool() ? 1.0f : 0.0f);
        juce::AudioBuffer<float> buf (2, 1 + rng.nextInt (700));
        fillNoise (buf, rng, 1.0f);
        p.processBlock (buf, midi);
        CHECK (allFinite (buf));
        CHECK (buf.getMagnitude (0, buf.getNumSamples()) < 64.0f); // +12 дБ громкости и шельфа поверх баса
    }
}

// Щелчки красной при включении и выключении. Синус 2 кГц, блоки по 64. Мерило — вторая разность выхода:
// у гладкого синуса она ~0.04 (0.5·(2π·2000/fs)²), скачок даёт величину порядка самого скачка.
void driveClicks (double sampleRate)
{
    GentraAudioProcessor p;
    p.setRateAndBufferSizeDetails (sampleRate, 64);
    p.prepareToPlay (sampleRate, 64);
    setParam (p, "drive", 50.0f);
    setParam (p, "distOn", 0.0f);

    long long t = 0;
    float y1 = 0.0f, y2 = 0.0f; // два прошлых сэмпла выхода — сквозь блоки и прогоны
    // Прогоняет seconds секунд и возвращает наибольшую вторую разность выхода после from секунд.
    const auto run = [&] (double seconds, double from)
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buf (2, 64);
        float jump = 0.0f;
        const auto blocks = static_cast<int> (seconds * sampleRate / 64.0);
        for (int b = 0; b < blocks; ++b)
        {
            for (int n = 0; n < 64; ++n)
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, n, 0.5f * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * 2000.0 * static_cast<double> (t + n) / sampleRate)));
            t += 64;
            p.processBlock (buf, midi);
            for (int n = 0; n < 64; ++n)
            {
                const float y = buf.getSample (0, n);
                if ((b * 64 + n) > from * sampleRate)
                    jump = juce::jmax (jump, std::abs (y - 2.0f * y1 + y2));
                y2 = y1;
                y1 = y;
            }
        }
        return jump;
    };

    run (0.1, 1.0e9);
    setParam (p, "distOn", 1.0f);
    const float on = run (0.002, -1.0); // первые 2 мс: Mix ещё почти 0, слышен бы только скачок фильтров
    run (0.2, 1.0e9);
    setParam (p, "distOn", 0.0f);
    const float off = run (0.3, 0.049); // Mix доехал до ~0, дальше — переход в обход
    std::printf ("drive clicks %.0f Hz: on %.3f, off %.3f\n", sampleRate, on, off);
    CHECK (on < 0.06f && off < 0.06f);
    CHECK (transparent (p, *std::make_unique<juce::Random> (1)));
}

// Шашки: за минуту поток перестраивается, машины в одной полосе не въезжают друг в друга
// — и в свободном потоке, и когда открытая машина забрала ближнюю полосу.
void traffic()
{
    GentraAudioProcessor p;
    for (int car : { -1, 0 })
    {
        RoadView road (p.apvts, nullptr);
        road.select (car);
        float gap = 1.0e9f;
        int flashFrames = 0;
        for (int i = 0; i < 60 * 60; ++i)
        {
            road.advance (1.0f / 60.0f);
            flashFrames += road.flashingCars() > 0;
            if (i > 60) // первая секунда — разъезд со стартовых мест
                gap = juce::jmin (gap, road.closestGap());
        }
        std::printf ("traffic car %d: %d lane changes, closest %.0f px\n", car, road.getLaneChanges(), gap);
        CHECK (road.getLaneChanges() >= 10);
        CHECK (gap > 100.0f);
        std::printf ("traffic car %d: someone flashing %.0f%% of the time, %d yields\n", car, 100.0 * flashFrames / 3600.0, road.getYields());
        CHECK (flashFrames > 0 && flashFrames < 3600 / 2); // моргают иногда — только быстрые
        CHECK (road.getYields() > 0);                     // и им уступают
    }

    // Выключенные уходят на обочину по одной полосе, а не сквозь соседние (с этой маской раньше въезжали).
    p.apvts.state.setProperty ("hiddenCars", (1 << 7) | (1 << 10), nullptr);
    for (int car : { -1, 0 })
    {
        RoadView road (p.apvts, nullptr);
        road.select (car);
        float gap = 1.0e9f;
        for (int i = 0; i < 60 * 60; ++i)
        {
            road.advance (1.0f / 60.0f);
            if (i > 60)
                gap = juce::jmin (gap, road.closestGap());
        }
        CHECK (gap > 100.0f);
    }
}

// Гараж: скрытая машина закрывается, если была открыта, выключает свой эффект, не мешает потоку,
// а маска живёт в пресете.
void garage()
{
    GentraAudioProcessor p;
    {
        RoadView road (p.apvts, nullptr);
        road.select (0);
        road.setHidden (0, true);
        road.setHidden (5, true);
        CHECK (road.getSelected() == -1 && road.isHidden (0) && road.isHidden (5) && ! road.isHidden (1));
        CHECK (p.apvts.getRawParameterValue ("bassOn")->load() < 0.5f); // скрытый эффект выключен
        road.setHidden (1, true);
        CHECK (p.apvts.getRawParameterValue ("loresOn")->load() < 0.5f);
        road.setHidden (1, false);
        CHECK (p.apvts.getRawParameterValue ("loresOn")->load() > 0.5f); // вернулся — включён, как был
        setParam (p, "loresOn", 0.0f);
        road.setHidden (1, true);
        road.setHidden (1, false);
        CHECK (p.apvts.getRawParameterValue ("loresOn")->load() < 0.5f); // был выключен — вернулся выключенным
        setParam (p, "loresOn", 1.0f);
        road.setHidden (5, false);
        road.setHidden (7, true);
        for (int i = 0; i < 600; ++i)
            road.advance (1.0f / 60.0f);
        CHECK (road.closestGap() > 100.0f);
    }
    juce::MemoryBlock state;
    p.getStateInformation (state);
    GentraAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    RoadView road (q.apvts, nullptr);
    CHECK (road.isHidden (0) && road.isHidden (7) && ! road.isHidden (5));
    CHECK (q.apvts.getRawParameterValue ("bassOn")->load() < 0.5f);
}

// Тряска под бит DAW: процессор правильно считает долю из позиции хоста, интерфейс бьёт на долях.

void beatSync()
{
    GentraAudioProcessor p;
    p.prepareToPlay (48000.0, 512);
    FakeHead head;
    p.setPlayHead (&head);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double beat = 0.0, bpm = 0.0;

    head.info.setIsPlaying (true);
    head.info.setBpm (120.0);
    head.info.setPpqPosition (9.5); // 4/4, такт с 8 — вторая доля, середина
    head.info.setPpqPositionOfLastBarStart (8.0);
    head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    buf.clear();
    p.processBlock (buf, midi);
    CHECK (p.beatNow (beat, bpm) && std::abs (beat - 1.5) < 0.02 && std::abs (bpm - 120.0) < 1.0e-6);

    head.info.setPpqPosition (7.5); // 6/8, такт с 6 — доля восьмая: четвёртая доля, темп в восьмых вдвое выше
    head.info.setPpqPositionOfLastBarStart (6.0);
    head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 6, 8 });
    p.processBlock (buf, midi);
    CHECK (p.beatNow (beat, bpm) && std::abs (beat - 3.0) < 0.05 && std::abs (bpm - 240.0) < 1.0e-6);

    head.info.setIsPlaying (false);
    p.processBlock (buf, midi);
    CHECK (! p.beatNow (beat, bpm));
    p.setPlayHead (nullptr);

    // Интерфейс: низ на максимуме, доля задаётся вручную.
    double at = 0.0;
    RoadView road (p.apvts, [] { return 1.0f; }, [&at] (double& b, double& t) { b = at; t = 120.0; return true; });
    const auto shakeAt = [&] (double position) { at = position; road.advance (1.0f / 60.0f); return road.getRumble(); };
    const float downbeat = shakeAt (0.02), weak = shakeAt (1.02), between = shakeAt (1.5);
    std::printf ("beat shake: downbeat %.2f, weak %.2f, between %.2f\n", downbeat, weak, between);
    CHECK (downbeat > 0.8f && weak > 0.4f && weak < downbeat && between < 0.2f);
}

// Масштаб окна: общий для всех экземпляров (настройки пользователя, не пресет), пропорции держатся.
void uiScale()
{
    GentraAudioProcessor p, other;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor->getWidth() == RoadView::width && editor->isResizable());
        editor->setSize (RoadView::width * 3 / 2, RoadView::height * 3 / 2);
    }
    CHECK (! p.apvts.state.hasProperty ("uiScale"));
    std::unique_ptr<juce::AudioProcessorEditor> editor (other.createEditor()); // другой экземпляр — тот же размер
    CHECK (editor->getWidth() == RoadView::width * 3 / 2 && editor->getHeight() == RoadView::height * 3 / 2);
}

// Тест пишет настройки во временный файл, а не в настоящие настройки пользователя.
void useTempSettings (double scale)
{
    auto& f = GentraAudioProcessorEditor::settingsFile;
    f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("GentraTest.settings");
    f.deleteFile();
    juce::PropertiesFile (f, {}).setValue ("uiScale", scale);
}

// --snapshot out.png [percent] [frames] [car] [scale]: рендерит интерфейс в PNG без окна; car — какую машину открыть.
int snapshot (const char* path, float percent, int frames, int car, double scale)
{
    useTempSettings (scale);
    GentraAudioProcessor p;
    setParam (p, "bass", percent);
    setParam (p, "lores", percent);
    setParam (p, "drive", percent);
    setParam (p, "eqVolume", (percent - 50.0f) * 0.24f); // −12…+12 дБ по шкале 0–100
    setParam (p, "eqHigh", 4.5f);
    setParam (p, "eqMid", 2.0f);
    setParam (p, "eqLow", -3.0f);
    setParam (p, "delayOn", percent > 0.0f ? 1.0f : 0.0f);
    setParam (p, "delayFeedback", 0.9f * percent);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    auto* gentra = dynamic_cast<GentraAudioProcessorEditor*> (editor.get());
    CHECK (gentra != nullptr);
    if (car == -2) // гараж с парой скрытых машин
    {
        p.apvts.state.setProperty ("hiddenCars", (1 << 3) | (1 << 7), nullptr);
        gentra->openGarage();
    }
    else
        gentra->select (car);
    for (int i = 0; i < frames; ++i)
        gentra->advance (1.0f / 60.0f);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f / static_cast<float> (scale));
    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (path));
    file.deleteFile();
    juce::FileOutputStream out (file);
    CHECK (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out));
    return 0;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && std::strcmp (argv[1], "--snapshot") == 0)
        return snapshot (argv[2], argc > 3 ? static_cast<float> (std::atof (argv[3])) : 0.0f, argc > 4 ? std::atoi (argv[4]) : 0,
                         argc > 5 ? std::atoi (argv[5]) : -1, argc > 6 ? std::atof (argv[6]) : 1.0);

    useTempSettings (1.0);
    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        run (sampleRate);
        equalizer (sampleRate);
        delayCar (sampleRate);
        automation (sampleRate);
    }
    driveClicks (44100.0);
    driveClicks (96000.0);
    traffic();
    uiScale();
    garage();
    beatSync();

    std::printf ("OK\n");
    return 0;
}
