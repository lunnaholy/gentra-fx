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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessor)
};
