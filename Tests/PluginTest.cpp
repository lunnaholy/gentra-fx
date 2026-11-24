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

    // 4. Обе на 100%: тишина остаётся тишиной.
    setParam (p, "bass", 100.0f);
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
        juce::AudioBuffer<float> buf (2, 1 + rng.nextInt (700));
        fillNoise (buf, rng, 1.0f);
        p.processBlock (buf, midi);
        CHECK (allFinite (buf));
        CHECK (buf.getMagnitude (0, buf.getNumSamples()) < 8.0f);
    }

    // 6. Вернули ручки в 0 — после сглаживания снова побитово прозрачно.
    setParam (p, "bass", 0.0f);
    setParam (p, "lores", 0.0f);
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
    settle (p, sampleRate, rng);
    CHECK (transparent (p, rng));
    setParam (p, "bassMix", 100.0f);
    setParam (p, "loresOn", 1.0f);
    setParam (p, "bass", 0.0f);
    setParam (p, "lores", 0.0f);

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

    // 9. Метр низа: бочка 50 Гц видна, тишина — ноль.
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

    // 10. Состояние переживает сохранение/загрузку.
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
        for (int i = 0; i < 60 * 60; ++i)
        {
            road.advance (1.0f / 60.0f);
            if (i > 60) // первая секунда — разъезд со стартовых мест
                gap = juce::jmin (gap, road.closestGap());
        }
        std::printf ("traffic car %d: %d lane changes, closest %.0f px\n", car, road.getLaneChanges(), gap);
        CHECK (road.getLaneChanges() >= 10);
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
        CHECK (p.apvts.getRawParameterValue ("loresOn")->load() > 0.5f); // вернулся — включён
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

// Масштаб окна: сохраняется в состоянии, новое окно открывается в том же размере, пропорции держатся.
void uiScale()
{
    GentraAudioProcessor p;
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        CHECK (editor->getWidth() == RoadView::width && editor->isResizable());
        editor->setSize (RoadView::width * 3 / 2, RoadView::height * 3 / 2);
    }
    CHECK (std::abs (static_cast<double> (p.apvts.state.getProperty ("uiScale")) - 1.5) < 0.01);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    CHECK (editor->getWidth() == RoadView::width * 3 / 2 && editor->getHeight() == RoadView::height * 3 / 2);
}

// --snapshot out.png [percent] [frames] [car] [scale]: рендерит интерфейс в PNG без окна; car — какую машину открыть.
int snapshot (const char* path, float percent, int frames, int car, double scale)
{
    GentraAudioProcessor p;
    p.apvts.state.setProperty ("uiScale", scale, nullptr);
    setParam (p, "bass", percent);
    setParam (p, "lores", percent);

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

    for (double sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        run (sampleRate);
    traffic();
    uiScale();
    garage();

    std::printf ("OK\n");
    return 0;
}
