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
    // Текстовые кнопки — маленькие металлические таблички.
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Image panel = RoadView::image ("panel_png");

    // Ползунок: пустой паз, поверх — светящийся до ручки, сверху ручка.
    juce::Image track = RoadView::image ("slider_track_png"), fill = RoadView::image ("slider_fill_png"),
                thumb = RoadView::image ("slider_thumb_png");
};

// Металлическая табличка (panel.png) любого размера: углы с винтами не тянутся; дети раскладываются снаружи.
struct Box final : juce::Component
{
    void paint (juce::Graphics&) override;
    juce::Image image = RoadView::image ("panel_png");
};

// Машина в гараже: нажата — едет по дороге, отжата — скрыта.
struct CarChip final : juce::Button
{
    CarChip() : juce::Button ({}) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    const RoadView* road = nullptr;
    int car = 0;
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
    void openGarage() { showGarage (true); }

    // Общие настройки пользователя (масштаб окна); тест подменяет файл, чтобы не трогать настоящий.
    static juce::File settingsFile;

private:
    static juce::PropertiesFile::Options settingsOptions();
    struct Row
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void showPopup (int car);
    void showGarage (bool);
    void layoutContent();
    void placePopup (int numRows);

    GentraAudioProcessor& gentraProcessor;
    juce::PropertiesFile settings { settingsFile, settingsOptions() };
    BoxLook look;

    // Весь интерфейс в базовом размере RoadView::width × height; окно масштабирует его целиком.
    juce::Component content;
    RoadView road;

    Box popup;
    juce::ImageButton onButton, closeButton;
    juce::TextButton syncButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;
    std::array<Row, 3> rows; // до трёх доп. параметров; табличка — под их число
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    // Гараж: какие машины на дороге.
    juce::TextButton garageButton;
    Box garage;
    juce::Label garageTitle;
    juce::ImageButton garageClose;
    std::array<CarChip, RoadView::numCars> chips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
