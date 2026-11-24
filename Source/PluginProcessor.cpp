#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
constexpr double smoothingSeconds = 0.05;
}

// ---------------- Stage ----------------

float GentraAudioProcessor::Stage::target (int i) const noexcept
{
    if ((i == wet && on->load() < 0.5f) || raw[static_cast<size_t> (i)] == nullptr)
        return 0.0f;
    return raw[static_cast<size_t> (i)]->load() * scale[static_cast<size_t> (i)];
}

void GentraAudioProcessor::Stage::prepare (double sampleRate) noexcept
{
    for (auto& v : value)
        v.reset (sampleRate, smoothingSeconds);
    snap();
}

void GentraAudioProcessor::Stage::snap() noexcept
{
    for (int i = 0; i < count; ++i)
        value[static_cast<size_t> (i)].setCurrentAndTargetValue (target (i));
}

void GentraAudioProcessor::Stage::updateTargets() noexcept
{
    for (int i = 0; i < count; ++i)
        value[static_cast<size_t> (i)].setTargetValue (target (i));
}

void GentraAudioProcessor::Stage::skip (int numSamples) noexcept
{
    for (auto& v : value)
        v.skip (numSamples);
}

// Машина простаивает, когда звучать нечему (параметры из aliveMask в нуле) или нечем (wet в нуле) и ничего не едет.
// Простаивающая стадия не трогает буфер — отсюда bit-perfect при 0%. Пустая aliveMask — звучит всегда (дилей).
bool GentraAudioProcessor::Stage::active() const noexcept
{
    bool hasEffect = aliveMask == 0;
    for (int i : { main, extraA, extraB })
        if ((aliveMask >> i) & 1)
        {
            if (value[static_cast<size_t> (i)].isSmoothing())
                return true;
            hasEffect = hasEffect || std::abs (value[static_cast<size_t> (i)].getTargetValue()) > 0.0f;
        }
    return value[wet].isSmoothing() || (hasEffect && value[wet].getTargetValue() > 0.0f);
}

// ---------------- Processor ----------------

GentraAudioProcessor::GentraAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      black (makeStage ("bass", 0.01f, "bassFreq", 1.0f, "tint", 0.01f, "bassMix", "bassOn", Stage::keep (Stage::main) | Stage::keep (Stage::extraB))),
      rust (makeStage ("lores", 0.01f, "loresBits", 1.0f, "loresRate", 1000.0f, "loresMix", "loresOn", Stage::keep (Stage::main))),
      red (makeStage ("drive", 0.01f, "distTone", 0.01f, nullptr, 0.0f, "distMix", "distOn", Stage::keep (Stage::main))),
      white (makeStage ("eqVolume", 1.0f, "eqLow", 1.0f, "eqHigh", 1.0f, "eqMix", "eqOn",
                        Stage::keep (Stage::main) | Stage::keep (Stage::extraA) | Stage::keep (Stage::extraB))),
      silver (makeStage ("delayTime", 0.001f, "delayFeedback", 0.01f, "delayTone", 0.01f, "delayMix", "delayOn", 0)),
      delaySync (apvts.getRawParameterValue ("delaySync")),
      delayTimeParam (apvts.getParameter ("delayTime"))
{
}

GentraAudioProcessor::Stage GentraAudioProcessor::makeStage (const char* mainId, float mainScale, const char* aId, float aScale,
                                                             const char* bId, float bScale,
                                                             const char* mixId, const char* onId, int aliveMask)
{
    Stage s;
    s.raw = { apvts.getRawParameterValue (mainId), apvts.getRawParameterValue (aId),
              bId != nullptr ? apvts.getRawParameterValue (bId) : nullptr, apvts.getRawParameterValue (mixId) };
    s.scale = { mainScale, aScale, bScale, 0.01f };
    s.on = apvts.getRawParameterValue (onId);
    s.aliveMask = aliveMask;
    return s;
}

juce::AudioProcessorValueTreeState::ParameterLayout GentraAudioProcessor::createParameterLayout()
{
    using Float = juce::AudioParameterFloat;
    using Attributes = juce::AudioParameterFloatAttributes;

    const juce::NormalisableRange<float> percent (0.0f, 100.0f, 0.1f);
    const auto pct = Attributes().withLabel ("%");

    juce::NormalisableRange<float> rate (0.5f, 12.0f, 0.01f);
    rate.setSkewForCentre (3.0f);

    // Шаг 0.1 дБ, но округление своё: штатное (−12 + 120·0.1) даёт вместо нуля 1.8e-7, и эквалайзер не простаивал бы.
    juce::NormalisableRange<float> decibels (-12.0f, 12.0f,
                                             [] (float start, float end, float t) { return start + (end - start) * t; },
                                             [] (float start, float end, float v) { return (v - start) / (end - start); },
                                             [] (float, float, float v) { return std::round (v * 10.0f) / 10.0f; });
    decibels.interval = 0.1f;
    const auto dB = Attributes().withLabel ("dB");

    juce::NormalisableRange<float> delayMs (60.0f, 600.0f, 1.0f);
    delayMs.setSkewForCentre (200.0f);

    // По темпу ручка времени выбирает долю такта — и хост показывает её же, а не миллисекунды.
    auto sync = std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delaySync", 3 }, "Delay Sync", false);
    const auto* syncParam = sync.get();
    auto time = std::make_unique<Float> (
        juce::ParameterID { "delayTime", 3 }, "Delay Time", delayMs, 250.0f,
        Attributes().withStringFromValueFunction ([syncParam, delayMs] (float ms, int)
        {
            if (syncParam->get())
                return juce::String (gentra::delayDivisionNames[static_cast<size_t> (gentra::delayDivision (delayMs.convertTo0to1 (ms)))]);
            return juce::String (juce::roundToInt (ms)) + " ms";
        }));

    // Версия 1 — параметры первого релиза, версия 2 — добавленные вместе с «дорожным» интерфейсом,
    // 3 — красная, белая и серебряная машины.
    juce::AudioProcessorValueTreeState::ParameterLayout layout {
        std::make_unique<Float> (juce::ParameterID { "bass", 1 }, "Bass Boost", percent, 0.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "bassFreq", 2 }, "Bass Freq",
                                 juce::NormalisableRange<float> (40.0f, 150.0f, 1.0f), 100.0f, Attributes().withLabel ("Hz")),
        std::make_unique<Float> (juce::ParameterID { "tint", 2 }, "Tint", percent, 0.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "bassMix", 2 }, "Bass Mix", percent, 100.0f, pct),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bassOn", 2 }, "Bass On", true),

        std::make_unique<Float> (juce::ParameterID { "lores", 1 }, "Lo-Res", percent, 0.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "loresBits", 2 }, "Lo-Res Min Bits",
                                 juce::NormalisableRange<float> (2.0f, 12.0f, 0.1f), 4.0f, Attributes().withLabel ("bit")),
        std::make_unique<Float> (juce::ParameterID { "loresRate", 2 }, "Lo-Res Min Rate", rate, 1.5f, Attributes().withLabel ("kHz")),
        std::make_unique<Float> (juce::ParameterID { "loresMix", 2 }, "Lo-Res Mix", percent, 100.0f, pct),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "loresOn", 2 }, "Lo-Res On", true),

        std::make_unique<Float> (juce::ParameterID { "drive", 3 }, "Drive", percent, 0.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "distTone", 3 }, "Drive Tone", percent, 50.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "distMix", 3 }, "Drive Mix", percent, 45.0f, pct),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "distOn", 3 }, "Drive On", true),

        std::make_unique<Float> (juce::ParameterID { "eqVolume", 3 }, "EQ Volume", decibels, 0.0f, dB),
        std::make_unique<Float> (juce::ParameterID { "eqLow", 3 }, "EQ Low", decibels, 0.0f, dB),
        std::make_unique<Float> (juce::ParameterID { "eqHigh", 3 }, "EQ High", decibels, 0.0f, dB),
        std::make_unique<Float> (juce::ParameterID { "eqMix", 3 }, "EQ Mix", percent, 100.0f, pct),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "eqOn", 3 }, "EQ On", true),

        std::make_unique<Float> (juce::ParameterID { "delayFeedback", 3 }, "Delay Feedback",
                                 juce::NormalisableRange<float> (0.0f, 90.0f, 0.1f), 35.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "delayTone", 3 }, "Delay Tone", percent, 60.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "delayMix", 3 }, "Delay Mix", percent, 30.0f, pct),
        // Дилей по умолчанию выключен: с Mix 30% он звучал бы сразу при вставке плагина.
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delayOn", 3 }, "Delay On", false)
    };
    layout.add (std::move (time), std::move (sync));
    return layout;
}

void GentraAudioProcessor::prepareToPlay (double sampleRate, int)
{
    bass.prepare (sampleRate);
    lores.prepare (sampleRate);
    meterCoeff = static_cast<float> (std::exp (-2.0 * gentra::pi * 150.0 / sampleRate));
    meterLp = 0.0f;
    black.prepare (sampleRate);
    rust.prepare (sampleRate);
    red.prepare (sampleRate);
    oversampling.initProcessing (driveChunk);
    drive.prepare (2.0 * sampleRate);
    driveEngaged.reset (sampleRate, 0.01);
    driveEngaged.setCurrentAndTargetValue (0.0f);
    white.prepare (sampleRate);
    silver.prepare (sampleRate);
    eq.prepare (sampleRate);
    delay.prepare (sampleRate);
}

void GentraAudioProcessor::reset()
{
    bass.reset();
    lores.reset();
    oversampling.reset();
    drive.reset();
    driveEngaged.setCurrentAndTargetValue (0.0f);
    eq.reset();
    delay.reset();
    meterLp = 0.0f;
    black.snap();
    rust.snap();
    red.snap();
    white.snap();
    silver.snap();
}

bool GentraAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void GentraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    readTransport();
    black.updateTargets();
    rust.updateTargets();

    const bool bassOn = black.active();
    const bool loresOn = rust.active();

    if (! bassOn)
    {
        bass.reset();
        black.skip (numSamples);
    }
    if (! loresOn)
    {
        lores.reset();
        rust.skip (numSamples);
    }
    const int numChannels = juce::jmin (buffer.getNumChannels(), gentra::maxChannels);
    auto* const* data = buffer.getArrayOfWritePointers();

    for (int n = 0; (bassOn || loresOn) && n < numSamples; ++n)
    {
        if (bassOn)
        {
            const float amount = black.next (Stage::main);
            const float freq = black.next (Stage::extraA);
            const float tint = black.next (Stage::extraB);
            const float wet = black.next (Stage::wet);
            bass.setParams (amount, freq, tint);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float x = data[ch][n];
                data[ch][n] = wet * bass.processSample (ch, x) + (1.0f - wet) * x; // при Mix 100% — ровно эффект
            }
        }

        if (loresOn)
        {
            const float amount = rust.next (Stage::main);
            const float bits = rust.next (Stage::extraA);
            const float rate = rust.next (Stage::extraB);
            const float wet = rust.next (Stage::wet);
            lores.setParams (amount, bits, rate);
            const bool capture = lores.tick();

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float x = data[ch][n];
                data[ch][n] = wet * lores.processSample (ch, x, capture) + (1.0f - wet) * x;
            }
        }
    }

    red.updateTargets();
    if (numChannels > 0)
        processDrive (buffer, numChannels);
    else
        red.skip (numSamples);

    processEqAndDelay (buffer, numChannels);

    // Метр низа: один полюс ~150 Гц по моно-сумме, пик держится, пока интерфейс его не заберёт.
    if (numChannels > 0)
    {
        float peak = 0.0f;
        for (int n = 0; n < numSamples; ++n)
        {
            const float mono = numChannels > 1 ? 0.5f * (data[0][n] + data[1][n]) : data[0][n];
            meterLp = mono + meterCoeff * (meterLp - mono);
            peak = juce::jmax (peak, std::abs (meterLp));
        }
        if (peak > bassLevel.load (std::memory_order_relaxed))
            bassLevel.store (peak, std::memory_order_relaxed);
    }
}

// Блоком, а не в общем цикле по сэмплам: оверсэмплинг работает кусками.
// В обходе через фильтры оверсэмплинга идёт копия входа, а буфер остаётся нетронутым (bit-perfect).
void GentraAudioProcessor::processDrive (juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), static_cast<size_t> (numChannels),
                                        static_cast<size_t> (buffer.getNumSamples()));
    std::array<std::array<float, driveChunk>, gentra::maxChannels> dry;
    std::array<float*, gentra::maxChannels> dryPtrs { dry[0].data(), dry[1].data() };

    for (size_t start = 0; start < block.getNumSamples(); start += driveChunk)
    {
        const size_t n = juce::jmin (static_cast<size_t> (driveChunk), block.getNumSamples() - start);
        auto sub = block.getSubBlock (start, n);
        juce::dsp::AudioBlock<float> dryBlock (dryPtrs.data(), static_cast<size_t> (numChannels), n);
        dryBlock.copyFrom (sub);

        driveEngaged.setTargetValue (red.active() ? 1.0f : 0.0f); // по кускам: обход наступает вовремя при любом блоке
        if (! driveEngaged.isSmoothing() && driveEngaged.getCurrentValue() <= 0.0f)
        {
            red.skip (static_cast<int> (n));
            drive.reset();
            oversampling.processSamplesUp (dryBlock);
            oversampling.processSamplesDown (dryBlock);
            continue;
        }

        std::array<float, driveChunk> amount {}, tone {}, wet {}, engaged {};
        for (size_t i = 0; i < n; ++i)
        {
            amount[i] = red.next (Stage::main);
            tone[i] = red.next (Stage::extraA);
            // Эффект плавно входит на первых процентах драйва: при 0% выход — сухой сигнал.
            wet[i] = red.next (Stage::wet) * juce::jmin (1.0f, amount[i] / gentra::fadeIn);
            engaged[i] = driveEngaged.getNextValue();
        }

        auto up = oversampling.processSamplesUp (sub);
        for (size_t k = 0; k < up.getNumSamples(); ++k)
        {
            const size_t i = k / 2;
            drive.setParams (amount[i], tone[i]);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            {
                const float x = up.getSample (static_cast<int> (ch), static_cast<int> (k));
                up.setSample (static_cast<int> (ch), static_cast<int> (k),
                              wet[i] * drive.processSample (static_cast<int> (ch), x) + (1.0f - wet[i]) * x);
            }
        }
        oversampling.processSamplesDown (sub);

        if (engaged[n - 1] < 1.0f || engaged[0] < 1.0f)
            for (size_t ch = 0; ch < static_cast<size_t> (numChannels); ++ch)
            {
                auto* y = sub.getChannelPointer (ch);
                for (size_t i = 0; i < n; ++i)
                    y[i] = dry[ch][i] + engaged[i] * (y[i] - dry[ch][i]);
            }
    }
}

// Белая и серебряная — после красной: эквалайзер, затем дилей.
void GentraAudioProcessor::processEqAndDelay (juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    const int numSamples = buffer.getNumSamples();
    auto* const* data = buffer.getArrayOfWritePointers();

    white.updateTargets();
    silver.updateTargets();
    const bool eqOn = white.active();
    const bool delayOn = silver.active();
    if (! eqOn)
    {
        eq.reset();
        white.skip (numSamples);
    }

    // По темпу — доля такта по положению ручки; время ручки (мс) тогда не нужно.
    const bool sync = delaySync->load() > 0.5f;
    const double syncSeconds = gentra::delayDivisionBeats[static_cast<size_t> (gentra::delayDivision (delayTimeParam->getValue()))]
                               * 60.0 / hostBpm.load (std::memory_order_relaxed);

    for (int n = 0; n < numSamples; ++n)
    {
        if (eqOn)
        {
            const float volume = white.next (Stage::main);
            const float low = white.next (Stage::extraA);
            const float high = white.next (Stage::extraB);
            const float wet = white.next (Stage::wet);
            eq.setParams (volume, low, high);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float x = data[ch][n];
                data[ch][n] = wet * eq.processSample (ch, x) + (1.0f - wet) * x;
            }
        }

        const float time = silver.next (Stage::main);
        const float feedback = silver.next (Stage::extraA);
        const float tone = silver.next (Stage::extraB);
        const float wet = silver.next (Stage::wet);
        delay.setParams (sync ? syncSeconds : static_cast<double> (time), feedback, tone);

        // Выключенный дилей не трогает буфер, но линия пишет вход: включили — повторы уже есть.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = data[ch][n];
            if (delayOn)
                data[ch][n] = wet * delay.processSample (ch, x) + (1.0f - wet) * x;
            else
                delay.feed (ch, x);
        }
        delay.advance();
    }
}

double GentraAudioProcessor::getTailLengthSeconds() const
{
    // Хвост дилея: пока повторы не затихнут до −60 дБ.
    if (apvts.getRawParameterValue ("delayOn")->load() < 0.5f || apvts.getRawParameterValue ("delayMix")->load() <= 0.0f)
        return 0.0;
    const double seconds = delaySync->load() > 0.5f
                               ? gentra::delayDivisionBeats[static_cast<size_t> (gentra::delayDivision (delayTimeParam->getValue()))]
                                     * 60.0 / hostBpm.load (std::memory_order_relaxed)
                               : apvts.getRawParameterValue ("delayTime")->load() * 0.001;
    const double feedback = apvts.getRawParameterValue ("delayFeedback")->load() * 0.01;
    const double repeats = feedback > 0.0 ? std::log (0.001) / std::log (feedback) : 0.0;
    return juce::jmin (gentra::delayMaxSeconds, seconds) * (1.0 + repeats);
}

void GentraAudioProcessor::readTransport() noexcept
{
    const auto* playHead = getPlayHead();
    const auto pos = playHead != nullptr ? playHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
    const auto ppq = pos ? pos->getPpqPosition() : juce::Optional<double>();
    const auto bpm = pos ? pos->getBpm() : juce::Optional<double>();
    if (bpm && *bpm > 0.0)
        hostBpm.store (*bpm, std::memory_order_relaxed); // темп для дилея — и когда транспорт стоит
    if (! pos || ! pos->getIsPlaying() || ! ppq || ! bpm || *bpm <= 0.0)
    {
        clockPlaying.store (false, std::memory_order_relaxed);
        return;
    }

    // Доля — по знаменателю размера (в 6/8 доля — восьмая), такт — от начала последнего такта.
    const auto sig = pos->getTimeSignature().orFallback (juce::AudioPlayHead::TimeSignature {});
    const double beatLen = 4.0 / juce::jmax (1, sig.denominator);
    const double barStart = pos->getPpqPositionOfLastBarStart().orFallback (0.0);
    clockBeat.store ((*ppq - barStart) / beatLen, std::memory_order_relaxed);
    clockBpm.store (*bpm / beatLen, std::memory_order_relaxed); // хост даёт четверти в минуту, нам — доли в минуту
    clockBeatsPerBar.store (juce::jmax (1, sig.numerator), std::memory_order_relaxed);
    clockStampMs.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    clockPlaying.store (true, std::memory_order_relaxed);
}

bool GentraAudioProcessor::beatNow (double& beatInBar, double& bpm) const noexcept
{
    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - clockStampMs.load (std::memory_order_relaxed);
    if (! clockPlaying.load (std::memory_order_relaxed) || elapsedMs > 500.0) // хост перестал звать processBlock
        return false;

    bpm = clockBpm.load (std::memory_order_relaxed);
    const double beat = clockBeat.load (std::memory_order_relaxed) + elapsedMs / 60000.0 * bpm;
    beatInBar = std::fmod (juce::jmax (0.0, beat), static_cast<double> (clockBeatsPerBar.load (std::memory_order_relaxed)));
    return true;
}

juce::AudioProcessorEditor* GentraAudioProcessor::createEditor()
{
    return new GentraAudioProcessorEditor (*this);
}

void GentraAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GentraAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GentraAudioProcessor();
}
