#pragma once

#include "PluginProcessor.h"
#include "RoadView.h"

// Стиль коробок из прототипа: серый фон, чёрная обводка, белые кнопки, жёлтый при нажатии.
struct BoxLook final : juce::LookAndFeel_V4
{
    BoxLook();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getLabelFont (juce::Label&) override;
};

// Серая коробка с чёрной обводкой; дети раскладываются снаружи.
struct Box final : juce::Component
{
    void paint (juce::Graphics&) override;
};

class GentraAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GentraAudioProcessorEditor (GentraAudioProcessor&);
    ~GentraAudioProcessorEditor() override;

    void resized() override;

    // Для теста: прокрутить анимацию и открыть машину без окна.
    void advance (float dt) { road.advance (dt); }
    void select (int car) { road.select (car); }

private:
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void showPopup (int car);
    void updateOnButton();

    GentraAudioProcessor& gentraProcessor;
    BoxLook look;

    RoadView road;

    Box popup;
    juce::TextButton onButton, closeButton { "X" };
    std::array<Row, 2> rows;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
