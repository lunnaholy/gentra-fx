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

// Машина простаивает, когда звучать нечему (главная ручка в нуле) или нечем (wet в нуле) и ничего не едет.
// Простаивающая стадия не трогает буфер — отсюда bit-perfect при 0%.
bool GentraAudioProcessor::Stage::active() const noexcept
{
    if (value[main].isSmoothing() || value[wet].isSmoothing() || (extraBKeepsAlive && value[extraB].isSmoothing()))
        return true;

    const bool hasEffect = value[main].getTargetValue() > 0.0f
                           || (extraBKeepsAlive && value[extraB].getTargetValue() > 0.0f);
    return hasEffect && value[wet].getTargetValue() > 0.0f;
}

// ---------------- Processor ----------------

GentraAudioProcessor::GentraAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout()),
      black (makeStage ("bass", "bassFreq", 1.0f, "tint", 0.01f, "bassMix", "bassOn", true)),
      rust (makeStage ("lores", "loresBits", 1.0f, "loresRate", 1000.0f, "loresMix", "loresOn", false)),
      red (makeStage ("drive", "distTone", 0.01f, nullptr, 0.0f, "distMix", "distOn", false))
{
}

GentraAudioProcessor::Stage GentraAudioProcessor::makeStage (const char* mainId, const char* aId, float aScale,
                                                             const char* bId, float bScale,
                                                             const char* mixId, const char* onId, bool bKeepsAlive)
{
    Stage s;
    s.raw = { apvts.getRawParameterValue (mainId), apvts.getRawParameterValue (aId),
              bId != nullptr ? apvts.getRawParameterValue (bId) : nullptr, apvts.getRawParameterValue (mixId) };
    s.scale = { 0.01f, aScale, bScale, 0.01f };
    s.on = apvts.getRawParameterValue (onId);
    s.extraBKeepsAlive = bKeepsAlive;
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

    // Версия 1 — параметры первого релиза, версия 2 — добавленные вместе с «дорожным» интерфейсом, 3 — красная машина.
    return {
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
        std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "distOn", 3 }, "Drive On", true)
    };
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
}

void GentraAudioProcessor::reset()
{
    bass.reset();
    lores.reset();
    oversampling.reset();
    drive.reset();
    driveEngaged.setCurrentAndTargetValue (0.0f);
    meterLp = 0.0f;
    black.snap();
    rust.snap();
    red.snap();
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

// Последняя в цепочке, поэтому блоком, а не в общем цикле по сэмплам: оверсэмплинг работает кусками.
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

void GentraAudioProcessor::readTransport() noexcept
{
    const auto* playHead = getPlayHead();
    const auto pos = playHead != nullptr ? playHead->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
    const auto ppq = pos ? pos->getPpqPosition() : juce::Optional<double>();
    const auto bpm = pos ? pos->getBpm() : juce::Optional<double>();
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
