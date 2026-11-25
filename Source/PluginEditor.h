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

// График белой машины: примерная АЧХ полос и громкости. Точки полос тянутся мышью, как в эквалайзере микшера FL:
// по горизонтали частота, по вертикали усиление; колесо над «Серединой» — ширина (Q), двойной клик — усиление в ноль.
class EqGraph final : public juce::Component, private juce::Timer
{
public:
    explicit EqGraph (juce::AudioProcessorValueTreeState&);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::Point<float> handle (int band) const; // где точка полосы: 0 низ, 1 середина, 2 верх

private:
    void timerCallback() override; // перерисовать, если параметры поменялись (в т.ч. из хоста)
    int bandAt (juce::Point<float>) const;
    juce::Rectangle<float> plot() const;
    float xOf (double hz) const;
    double hzAt (float x) const;
    float yOf (double db) const;
    double dbAt (float y) const;
    float value (const char* id) const;
    void set (const char* id, float v);
    gentra::EqSettings current() const;

    juce::AudioProcessorValueTreeState& apvts;
    gentra::EqSettings shown;
    gentra::Equalizer curve; // те же фильтры, что в звуке, — только для расчёта АЧХ
    int dragging = -1, hovered = -1;
};

// Гараж: все машины списком. Сверху вниз — порядок цепочки эффектов, меняется перетаскиванием;
// тумблер справа — едет машина по дороге или стоит в гараже; клик по строке открывает машину на дороге.
class GaragePanel final : public juce::Component
{
public:
    GaragePanel (GentraAudioProcessor&, RoadView&);
    static constexpr int width = 312;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::ImageButton close;

private:
    juce::Rectangle<float> rowBounds (int slot) const;
    juce::Rectangle<float> toggleBounds (juce::Rectangle<float> row) const;
    int slotAt (float y) const;

    GentraAudioProcessor& processor;
    RoadView& road;
    juce::Image image = RoadView::image ("panel_png");
    GentraAudioProcessor::CarOrder dragOrder {}; // порядок во время перетаскивания; в процессор — по отпусканию
    int dragCar = -1, hoverSlot = -1;
    float dragY = 0.0f, grab = 0.0f; // верх перетаскиваемой строки и где за неё взялись
    bool dragging = false;
    mutable std::array<float, RoadView::numCars> shownY {}; // строки доезжают до своих мест (рисуется 60 раз в секунду вместе с дорогой)
    mutable bool placed = false;
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
    void openGarage() { showGarage (true, false); }
    EqGraph& getEqGraph() { return eqGraph; }

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
    void showGarage (bool show, bool animate = true);
    void layoutContent();
    void placePopup (int numRows);
    static juce::Rectangle<int> garageArea (bool open);
    int popupRows = 3;
    bool popupGraph = false; // табличка белой машины — шире, с графиком

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
    EqGraph eqGraph;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    // Гараж: какие машины на дороге.
    juce::TextButton garageButton;
    GaragePanel garage;
    bool garageOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
