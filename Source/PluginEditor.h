#pragma once

#include "PluginProcessor.h"
#include "RoadView.h"

// Стиль всплывающей таблички: шрифт интерфейса, чёрный текст и ползунки.
struct BoxLook final : juce::LookAndFeel_V4
{
    BoxLook();
    juce::Font getLabelFont (juce::Label&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;

    // Ползунок: пустой паз, поверх — светящийся до ручки, сверху ручка.
    juce::Image track = RoadView::image ("slider_track_png"), fill = RoadView::image ("slider_fill_png"),
                thumb = RoadView::image ("slider_thumb_png");
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
    void layoutContent();

    GentraAudioProcessor& gentraProcessor;
    BoxLook look;

    // Весь интерфейс в базовом размере RoadView::width × height; окно масштабирует его целиком.
    juce::Component content;
    RoadView road;

    Box popup;
    juce::ImageButton onButton, closeButton;
    std::array<Row, 2> rows;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
