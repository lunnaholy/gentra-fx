#pragma once

#include "PluginProcessor.h"
#include "RoadView.h"

// Стиль всплывающей таблички: шрифт интерфейса, чёрный текст и ползунки.
struct BoxLook final : juce::LookAndFeel_V4
{
    BoxLook();
    juce::Font getLabelFont (juce::Label&) override;
};

// Металлическая табличка (panel.png); дети раскладываются снаружи.
struct Box final : juce::Component
{
    void paint (juce::Graphics&) override;
    juce::Image image = RoadView::image ("panel_png");
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

    GentraAudioProcessor& gentraProcessor;
    BoxLook look;

    RoadView road;

    Box popup;
    juce::ImageButton onButton, closeButton;
    std::array<Row, 2> rows;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
