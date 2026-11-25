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
    for (int i : { main, extraA, extraB, extraC })
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
                        Stage::keep (Stage::main) | Stage::keep (Stage::extraA) | Stage::keep (Stage::extraB) | Stage::keep (Stage::extraC),
                        "eqMid", 1.0f)),
      silver (makeStage ("delayTime", 0.001f, "delayFeedback", 0.01f, "delayTone", 0.01f, "delayMix", "delayOn", 0)),
      blue (makeStage ("chorusDepth", 0.01f, "chorusRate", 1.0f, "chorusFeedback", 0.01f, "chorusMix", "chorusOn", Stage::keep (Stage::main))),
      pink (makeStage ("phaserDepth", 0.01f, "phaserRate", 1.0f, "phaserFeedback", 0.01f, "phaserMix", "phaserOn", Stage::keep (Stage::main))),
      delaySync (apvts.getRawParameterValue ("delaySync")),
      delayNote (apvts.getRawParameterValue ("delayNote"))
{
    storeChain (getCarOrder());
    playingChain = chain.load();
    chainGain.setCurrentAndTargetValue (1.0f);
}

GentraAudioProcessor::Stage GentraAudioProcessor::makeStage (const char* mainId, float mainScale, const char* aId, float aScale,
                                                             const char* bId, float bScale,
                                                             const char* mixId, const char* onId, int aliveMask,
                                                             const char* cId, float cScale)
{
    const auto raw = [this] (const char* id) { return id != nullptr ? apvts.getRawParameterValue (id) : nullptr; };
    Stage s;
    s.raw = { raw (mainId), raw (aId), raw (bId), raw (cId), raw (mixId) };
    s.scale = { mainScale, aScale, bScale, cScale, 0.01f };
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

    juce::NormalisableRange<float> lfoHz (0.1f, 8.0f, 0.01f);
    lfoHz.setSkewForCentre (1.0f);
    const auto hz = Attributes().withLabel ("Hz");
    const juce::NormalisableRange<float> feedback (0.0f, 90.0f, 0.1f);

    juce::StringArray notes;
    for (const auto* name : gentra::delayDivisionNames)
        notes.add (name);

    // Версия 1 — параметры первого релиза, версия 2 — добавленные вместе с «дорожным» интерфейсом,
    // 3 — красная, белая и серебряная машины, 4 — синяя, розовая и серая.
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
        std::make_unique<Float> (juce::ParameterID { "eqMid", 3 }, "EQ Mid", decibels, 0.0f, dB),
        std::make_unique<Float> (juce::ParameterID { "eqHigh", 3 }, "EQ High", decibels, 0.0f, dB),
        std::make_unique<Float> (juce::ParameterID { "eqMix", 3 }, "EQ Mix", percent, 100.0f, pct),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "eqOn", 3 }, "EQ On", true),

        std::make_unique<Float> (juce::ParameterID { "delayFeedback", 3 }, "Delay Feedback",
                                 juce::NormalisableRange<float> (0.0f, 90.0f, 0.1f), 35.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "delayTone", 3 }, "Delay Tone", percent, 60.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "delayMix", 3 }, "Delay Mix", percent, 30.0f, pct),
        // Дилей по умолчанию выключен: с Mix 30% он звучал бы сразу при вставке плагина.
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delayOn", 3 }, "Delay On", false),
        // По умолчанию время — длительность ноты по темпу хоста; «По такту» выключен — миллисекунды.
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delaySync", 3 }, "Delay Sync", true),
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "delayNote", 3 }, "Delay Note", notes, gentra::defaultDelayDivision),
        std::make_unique<Float> (juce::ParameterID { "delayTime", 3 }, "Delay Time", delayMs, 250.0f, Attributes().withLabel ("ms")),

        std::make_unique<Float> (juce::ParameterID { "chorusDepth", 4 }, "Chorus Depth", percent, 0.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "chorusRate", 4 }, "Chorus Rate", lfoHz, 1.0f, hz),
        std::make_unique<Float> (juce::ParameterID { "chorusFeedback", 4 }, "Chorus Feedback", feedback, 0.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "chorusMix", 4 }, "Chorus Mix", percent, 50.0f, pct),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "chorusOn", 4 }, "Chorus On", true),

        std::make_unique<Float> (juce::ParameterID { "phaserDepth", 4 }, "Phaser Depth", percent, 0.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "phaserRate", 4 }, "Phaser Rate", lfoHz, 0.5f, hz),
        std::make_unique<Float> (juce::ParameterID { "phaserFeedback", 4 }, "Phaser Feedback", feedback, 50.0f, pct),
        std::make_unique<Float> (juce::ParameterID { "phaserMix", 4 }, "Phaser Mix", percent, 50.0f, pct),
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "phaserOn", 4 }, "Phaser On", true)
    };
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
    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (fxChunk), static_cast<juce::uint32> (gentra::maxChannels) };
    blue.prepare (sampleRate);
    chorus.prepare (spec);
    chorus.setMix (1.0f); // Mix — свой, посэмплово
    pink.prepare (sampleRate);
    phaser.prepare (spec);
    phaser.setMix (1.0f);
    chainGain.reset (sampleRate, 0.01);
    playingChain = chain.load();
    chainGain.setCurrentAndTargetValue (1.0f);
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
    chorus.reset();
    phaser.reset();
    meterLp = 0.0f;
    black.snap();
    rust.snap();
    red.snap();
    white.snap();
    silver.snap();
    blue.snap();
    pink.snap();
    playingChain = chain.load();
    chainGain.setCurrentAndTargetValue (1.0f);
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
    const int numChannels = juce::jmin (buffer.getNumChannels(), gentra::maxChannels);
    auto* const* data = buffer.getArrayOfWritePointers();

    // Новый порядок — только когда громкость уже ушла в ноль.
    const auto wanted = chain.load (std::memory_order_relaxed);
    if (wanted != playingChain && chainGain.getCurrentValue() <= 0.0f)
        playingChain = wanted;
    chainGain.setTargetValue (wanted == playingChain ? 1.0f : 0.0f);

    for (auto order = playingChain; order != 0; order >>= 4)
        processCar (static_cast<int> (order & 15) - 1, buffer, numChannels);

    if (chainGain.isSmoothing() || chainGain.getCurrentValue() < 1.0f)
        for (int n = 0; n < numSamples; ++n)
        {
            const float gain = chainGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                data[ch][n] *= gain;
        }

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

void GentraAudioProcessor::processCar (int car, juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    switch (car)
    {
        case gentra::car::black: processBass (buffer, numChannels); break;
        case gentra::car::rustCar: processLores (buffer, numChannels); break;
        case gentra::car::red:
            red.updateTargets();
            if (numChannels > 0)
                processDrive (buffer, numChannels);
            else
                red.skip (buffer.getNumSamples());
            break;
        case gentra::car::white: processEq (buffer, numChannels); break;
        case gentra::car::silver: processDelay (buffer, numChannels); break;
        case gentra::car::blue:
            processChunked (blue, buffer, numChannels,
                            [this] (juce::dsp::AudioBlock<float>& block, float depth, float rate, float feedback)
                            {
                                chorus.setDepth (depth);
                                chorus.setRate (rate);
                                chorus.setFeedback (feedback);
                                chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
                            },
                            [this] { chorus.reset(); });
            break;
        case gentra::car::pink:
            processChunked (pink, buffer, numChannels,
                            [this] (juce::dsp::AudioBlock<float>& block, float depth, float rate, float feedback)
                            {
                                phaser.setDepth (depth);
                                phaser.setRate (rate);
                                phaser.setFeedback (feedback);
                                phaser.process (juce::dsp::ProcessContextReplacing<float> (block));
                            },
                            [this] { phaser.reset(); });
            break;
        default: break;
    }
}

template <typename Apply, typename Reset>
void GentraAudioProcessor::processChunked (Stage& s, juce::AudioBuffer<float>& buffer, int numChannels, Apply&& apply, Reset&& reset) noexcept
{
    s.updateTargets();
    if (! s.active() || numChannels == 0)
    {
        reset();
        s.skip (buffer.getNumSamples());
        return;
    }

    auto* const* data = buffer.getArrayOfWritePointers();
    std::array<std::array<float, fxChunk>, gentra::maxChannels> wetSignal;
    std::array<float*, gentra::maxChannels> ptrs { wetSignal[0].data(), wetSignal[1].data() };
    std::array<float, fxChunk> wet {};

    for (int start = 0; start < buffer.getNumSamples(); start += fxChunk)
    {
        const int n = juce::jmin (fxChunk, buffer.getNumSamples() - start);
        float main = 0.0f, a = 0.0f, b = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            main = s.next (Stage::main);
            a = s.next (Stage::extraA);
            b = s.next (Stage::extraB);
            // Эффект плавно входит на первых процентах главной ручки: у этих блоков и около нуля есть окраска.
            wet[static_cast<size_t> (i)] = s.next (Stage::wet) * juce::jmin (1.0f, main / gentra::fadeIn);
        }

        for (int ch = 0; ch < gentra::maxChannels; ++ch) // моно — дублируем первый канал
            std::copy_n (data[juce::jmin (ch, numChannels - 1)] + start, n, wetSignal[static_cast<size_t> (ch)].data());
        juce::dsp::AudioBlock<float> block (ptrs.data(), static_cast<size_t> (gentra::maxChannels), static_cast<size_t> (n));
        apply (block, main, a, b);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* y = data[ch] + start;
            const auto& w = wetSignal[static_cast<size_t> (ch)];
            for (int i = 0; i < n; ++i)
                y[i] += wet[static_cast<size_t> (i)] * (w[static_cast<size_t> (i)] - y[i]);
        }
    }
}

void GentraAudioProcessor::processBass (juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    black.updateTargets();
    if (! black.active())
    {
        bass.reset();
        black.skip (buffer.getNumSamples());
        return;
    }
    auto* const* data = buffer.getArrayOfWritePointers();
    for (int n = 0; n < buffer.getNumSamples(); ++n)
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
}

void GentraAudioProcessor::processLores (juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    rust.updateTargets();
    if (! rust.active())
    {
        lores.reset();
        rust.skip (buffer.getNumSamples());
        return;
    }
    auto* const* data = buffer.getArrayOfWritePointers();
    for (int n = 0; n < buffer.getNumSamples(); ++n)
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

void GentraAudioProcessor::processEq (juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    white.updateTargets();
    if (! white.active())
    {
        eq.reset();
        white.skip (buffer.getNumSamples());
        return;
    }
    auto* const* data = buffer.getArrayOfWritePointers();
    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
        const float volume = white.next (Stage::main);
        const float low = white.next (Stage::extraA);
        const float high = white.next (Stage::extraB);
        const float mid = white.next (Stage::extraC);
        const float wet = white.next (Stage::wet);
        eq.setParams (volume, low, mid, high);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = data[ch][n];
            data[ch][n] = wet * eq.processSample (ch, x) + (1.0f - wet) * x;
        }
    }
}

void GentraAudioProcessor::processDelay (juce::AudioBuffer<float>& buffer, int numChannels) noexcept
{
    silver.updateTargets();
    const bool delayOn = silver.active();
    auto* const* data = buffer.getArrayOfWritePointers();

    // По темпу — длительность выбранной ноты; время ручки (мс) тогда не нужно.
    const bool sync = delaySync->load() > 0.5f;
    const double syncSeconds = gentra::delayDivisionSeconds (delayNote->load(), hostBpm.load (std::memory_order_relaxed));

    for (int n = 0; n < buffer.getNumSamples(); ++n)
    {
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
                               ? gentra::delayDivisionSeconds (delayNote->load(), hostBpm.load (std::memory_order_relaxed))
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
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            storeChain (getCarOrder());
        }
}

namespace
{
const juce::Identifier carOrderId { "carOrder" }; // номера машин через пробел
}

GentraAudioProcessor::CarOrder GentraAudioProcessor::getCarOrder() const
{
    // Порча или старый пресет без порядка — порядок по умолчанию (каждый номер ровно один раз).
    const auto tokens = juce::StringArray::fromTokens (apvts.state.getProperty (carOrderId).toString(), false);
    if (tokens.size() != gentra::car::count)
        return defaultOrder;
    CarOrder order {};
    int seen = 0;
    for (int i = 0; i < gentra::car::count; ++i)
    {
        const int car = tokens[i].getIntValue();
        if (car < 0 || car >= gentra::car::count || ((seen >> car) & 1))
            return defaultOrder;
        seen |= 1 << car;
        order[static_cast<size_t> (i)] = car;
    }
    return order;
}

void GentraAudioProcessor::setCarOrder (const CarOrder& order)
{
    juce::StringArray tokens;
    for (int car : order)
        tokens.add (juce::String (car));
    apvts.state.setProperty (carOrderId, tokens.joinIntoString (" "), nullptr);
    storeChain (getCarOrder());
}

void GentraAudioProcessor::storeChain (const CarOrder& order) noexcept
{
    namespace c = gentra::car;
    constexpr int sounding = (1 << c::black) | (1 << c::rustCar) | (1 << c::red) | (1 << c::white) | (1 << c::silver) | (1 << c::blue)
                            | (1 << c::pink);
    std::uint64_t packed = 0;
    int shift = 0;
    for (int car : order)
        if ((sounding >> car) & 1)
        {
            packed |= static_cast<std::uint64_t> (car + 1) << shift;
            shift += 4;
        }
    chain.store (packed, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GentraAudioProcessor();
}
