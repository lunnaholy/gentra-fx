#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Dsp.h"

class GentraAudioProcessor final : public juce::AudioProcessor
{
public:
    GentraAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Пик низа (< ~150 Гц) на выходе с прошлого вызова — для тряски дороги в интерфейсе.
    float takeBassLevel() noexcept { return bassLevel.exchange (0.0f); }

    // Где сейчас транспорт DAW: доля внутри такта (0 — сильная доля, дробная часть — фаза доли) и темп в долях в минуту.
    // Между блоками досчитывается по темпу. false — транспорт стоит или хост не сообщает позицию.
    bool beatNow (double& beatInBar, double& bpm) const noexcept;

private:
    // Сглаженные параметры одной машины: главная ручка, два доп. параметра, итоговый wet (Mix с учётом вкл/выкл).
    struct Stage
    {
        enum Index { main, extraA, extraB, wet, count };

        std::array<std::atomic<float>*, count> raw {}; // для wet — параметр Mix
        std::array<float, count> scale {};
        std::atomic<float>* on = nullptr;
        bool extraBKeepsAlive = false; // тонировка чёрной звучит и при нулевом басе
        std::array<juce::SmoothedValue<float>, count> value;

        float target (int i) const noexcept;
        void prepare (double sampleRate) noexcept;
        void snap() noexcept;
        void updateTargets() noexcept;
        void skip (int numSamples) noexcept;
        bool active() const noexcept;
        float next (int i) noexcept { return value[static_cast<size_t> (i)].getNextValue(); }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    Stage makeStage (const char* mainId, const char* aId, float aScale, const char* bId, float bScale,
                     const char* mixId, const char* onId, bool bKeepsAlive);

    Stage black, rust;
    gentra::BassBoost bass;
    gentra::LoRes lores;

    std::atomic<float> bassLevel { 0.0f };

    // Снимок транспорта на начало последнего блока (пишет аудиопоток, читает интерфейс).
    std::atomic<bool> clockPlaying { false };
    std::atomic<double> clockBeat { 0.0 }, clockBpm { 120.0 }, clockStampMs { 0.0 };
    std::atomic<int> clockBeatsPerBar { 4 };
    void readTransport() noexcept;
    float meterLp = 0.0f, meterCoeff = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessor)
};
