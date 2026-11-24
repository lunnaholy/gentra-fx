#pragma once

#include "PluginProcessor.h"

class GentraAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GentraAudioProcessorEditor (GentraAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& title);

    juce::Slider bassSlider, loresSlider;
    juce::Label bassLabel, loresLabel;
    SliderAttachment bassAttachment, loresAttachment; // после слайдеров: уничтожаются первыми

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
