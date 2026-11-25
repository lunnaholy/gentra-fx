#pragma once

#include "PluginProcessor.h"
#include "RoadView.h"

// Металлическая табличка (panel.png) любого размера: углы с винтами не тянутся. Собирается из девяти кусков один раз
// под пиксели экрана и дальше рисуется готовой: всё окно перерисовывается 60 раз в секунду вместе с дорогой.
struct Panel
{
    void draw (juce::Graphics&, juce::Rectangle<float> area, float corner);

    juce::Image source = RoadView::image ("panel_png");
    std::array<juce::Image, 4> ready; // под разные размеры: один стиль рисует несколько кнопок
    size_t next = 0;
};

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
    Panel panel;

    // Ползунок: пустой паз, поверх — светящийся до ручки, сверху ручка.
    juce::Image track = RoadView::image ("slider_track_png"), fill = RoadView::image ("slider_fill_png"),
                thumb = RoadView::image ("slider_thumb_png");
};

// Металлическая табличка (panel.png) любого размера: углы с винтами не тянутся; дети раскладываются снаружи.
struct Box final : juce::Component
{
    void paint (juce::Graphics&) override;
    Panel image;
};

// «О плагине»: затемнение на всё окно и металлическая табличка с названием, версией, ссылками и копирайтом.
// Закрывается крестиком или кликом мимо таблички.
class AboutBox final : public juce::Component
{
public:
    AboutBox();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    juce::ImageButton close;

private:
    juce::Rectangle<int> panel() const;
    std::vector<std::unique_ptr<juce::HyperlinkButton>> links;
    Panel image;
};

// Вход и выход всей цепочки: гейн в дБ (двойной клик — 0) и индикаторы пика L/R со спадом, удержанием пика
// и лампой перегруза. Металлическая табличка сверху посередине.
class IoPanel final : public juce::Component, private juce::Timer
{
public:
    explicit IoPanel (GentraAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr float floorDb = -48.0f, ceilingDb = 6.0f; // шкала индикатора

private:
    void timerCallback() override;

    struct Side
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        juce::Rectangle<float> meter;
        std::array<float, 2> shownDb { floorDb, floorDb }, holdDb { floorDb, floorDb };
        std::array<double, 2> holdUntil {};
        double clipUntil = 0.0;
    };
    GentraAudioProcessor& processor;
    std::array<Side, 2> sides; // 0 — вход, 1 — выход
    double lastTick = 0.0;
    Panel image;
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

// Экран сайдчейна, как в Kickstart: громкость на один шаг ноты (провал и возврат по «Форме», с глубиной и Mix)
// и бегущая линия — где сейчас фаза шага (по позиции DAW, без транспорта — по последнему темпу).
class DuckGraph final : public juce::Component, private juce::Timer
{
public:
    explicit DuckGraph (GentraAudioProcessor&);
    void paint (juce::Graphics&) override;
    double phaseNow() const; // фаза шага 0..1 для бегущей линии
    float gainAt (double phase) const; // громкость на кривой 0..1

private:
    void timerCallback() override { if (isShowing()) repaint(); }
    float value (const char* id) const { return processor.apvts.getRawParameterValue (id)->load(); }
    GentraAudioProcessor& processor;
};

// Гараж: все машины списком. Сверху вниз — порядок цепочки эффектов, меняется перетаскиванием;
// тумблер справа — едет машина по дороге или стоит в гараже; клик по строке открывает машину на дороге.
class GaragePanel final : public juce::Component, private juce::Timer
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
    Panel image;
    GentraAudioProcessor::CarOrder dragOrder {}; // порядок во время перетаскивания; в процессор — по отпусканию
    int dragCar = -1, hoverSlot = -1;
    float dragY = 0.0f, grab = 0.0f; // верх перетаскиваемой строки и где за неё взялись
    bool dragging = false;
    // Панель рисуется в картинку и перерисовывается, только когда что-то поменялось: дорога под ней обновляется
    // 60 раз в секунду, а двенадцать строк с текстом — дорогой кадр. Таймер следит за тем, что меняется не мышью.
    void timerCallback() override;
    int stateNow() const; // порядок, скрытые и открытая машина одним числом
    int shownState = 0;
    mutable std::array<float, RoadView::numCars> shownY {}; // строки доезжают до своих мест
    mutable bool placed = false, moving = false;
};

class GentraAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit GentraAudioProcessorEditor (GentraAudioProcessor&);
    ~GentraAudioProcessorEditor() override;

    void resized() override;

    // Окно на 100% — сцена RoadView::width × height, увеличенная во столько раз (масштаб пользователя — поверх).
    static constexpr double baseScale = 1.2;

    // Для теста: прокрутить анимацию и открыть машину без окна.
    void advance (float dt) { road.advance (dt); }
    void select (int car) { road.select (car); }
    void openGarage() { showGarage (true, false); }
    EqGraph& getEqGraph() { return eqGraph; }
    void showAbout (bool show);
    bool isAboutShown() const { return about.isVisible(); }

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
    juce::Component* popupGraph = nullptr; // экран в табличке (эквалайзер, сайдчейн) — табличка шире
    bool popupTrigger = false; // у машины есть педаль — табличка шире на кнопку

    GentraAudioProcessor& gentraProcessor;
    juce::PropertiesFile settings { settingsFile, settingsOptions() };
    BoxLook look;

    // Весь интерфейс в базовом размере RoadView::width × height; окно масштабирует его целиком.
    juce::Component content;
    RoadView road;

    Box popup;
    juce::ImageButton onButton, closeButton;
    juce::TextButton syncButton, triggerButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment, triggerAttachment;
    std::array<Row, 3> rows; // до трёх доп. параметров; табличка — под их число
    EqGraph eqGraph;
    DuckGraph duckGraph;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;

    IoPanel io;
    AboutBox about;

    // Гараж: какие машины на дороге.
    juce::TextButton garageButton;
    GaragePanel garage;
    bool garageOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
