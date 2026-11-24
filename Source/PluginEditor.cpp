#include "PluginEditor.h"

namespace
{
constexpr float degrees = juce::MathConstants<float>::pi / 180.0f;

juce::String ru (const char* s) { return juce::String::fromUTF8 (s); }

constexpr double minScale = 0.6, maxScale = 2.0;
const juce::String uiScaleKey { "uiScale" };

// Доп. параметры машины во всплывающей коробке (главная ручка и Mix — на колёсах).
struct Extra
{
    const char* id;
    const char* label;
    const char* unit;
    int decimals;
};
const std::array<std::array<Extra, 2>, RoadView::numWorking> extras { {
    { { { "bassFreq", "Частота", "Гц", 0 }, { "tint", "Тонировка", "%", 0 } } },
    { { { "loresBits", "Мин. биты", "бит", 1 }, { "loresRate", "Мин. частота", "кГц", 1 } } },
} };

// Картинка с неизменными углами: srcCorner пикселей картинки ложатся в dstCorner пикселей окна.
void drawNine (juce::Graphics& g, const juce::Image& img, juce::Rectangle<float> r, int srcCorner, float dstCorner)
{
    const int iw = img.getWidth(), ih = img.getHeight(), c = srcCorner;
    const int sx[] = { 0, c, iw - c, iw }, sy[] = { 0, c, ih - c, ih };
    const float dx[] = { r.getX(), r.getX() + dstCorner, r.getRight() - dstCorner, r.getRight() };
    const float dy[] = { r.getY(), r.getY() + dstCorner, r.getBottom() - dstCorner, r.getBottom() };
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            g.drawImage (img.getClippedImage ({ sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row] }),
                         { dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row] });
}

void rotate (juce::Component& c, float deg)
{
    c.setTransform (juce::AffineTransform::rotation (deg * degrees, c.getBounds().toFloat().getCentreX(), c.getBounds().toFloat().getCentreY()));
}
}

// ---------------- стиль ----------------

BoxLook::BoxLook()
{
    setColour (juce::Label::textColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

juce::Font BoxLook::getLabelFont (juce::Label&)
{
    return RoadView::font (16.0f);
}

void BoxLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    g.setOpacity (1.0f);
    drawNine (g, panel, b.getLocalBounds().toFloat(), 70, 14.0f);
    if (over || down)
    {
        g.setColour (juce::Colours::black.withAlpha (down ? 0.25f : 0.12f));
        g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (3.0f), 4.0f);
    }
}

juce::Font BoxLook::getTextButtonFont (juce::TextButton&, int)
{
    return RoadView::font (17.0f);
}

void CarChip::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (over || down)
    {
        g.setColour (juce::Colours::black.withAlpha (down ? 0.18f : 0.09f));
        g.fillRoundedRectangle (r, 6.0f);
    }
    const bool shown = getToggleState();
    const auto label = r.removeFromBottom (18.0f);
    if (road != nullptr)
        road->drawThumbnail (g, car, r.reduced (6.0f, 3.0f), shown ? 1.0f : 0.22f);
    g.setColour (juce::Colours::black.withAlpha (shown ? 1.0f : 0.35f));
    g.setFont (RoadView::font (15.0f));
    g.drawText (RoadView::effectName (car), label, juce::Justification::centred);
}

constexpr int thumbHalf = 12; // ручка ~24×22; ползунок ходит в пределах, где она целиком внутри паза

int BoxLook::getSliderThumbRadius (juce::Slider&)
{
    return thumbHalf;
}

void BoxLook::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                juce::Slider::SliderStyle, juce::Slider&)
{
    // x..x+width — ход центра ручки; паз шире на полручки с каждой стороны.
    const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    const auto groove = juce::Rectangle<float> (static_cast<float> (x - thumbHalf), cy - 8.0f,
                                                static_cast<float> (width + 2 * thumbHalf), 16.0f);
    g.setOpacity (1.0f);
    g.drawImage (track, groove);
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (groove.withRight (sliderPos).getSmallestIntegerContainer());
        g.drawImage (fill, groove);
    }
    const float tw = 2.0f * thumbHalf, th = tw * static_cast<float> (thumb.getHeight()) / static_cast<float> (thumb.getWidth());
    g.drawImage (thumb, { sliderPos - tw * 0.5f, cy - th * 0.5f, tw, th });
}

void Box::paint (juce::Graphics& g)
{
    g.setOpacity (1.0f);
    drawNine (g, image, getLocalBounds().toFloat(), 70, 35.0f);
}

// ---------------- редактор ----------------

GentraAudioProcessorEditor::GentraAudioProcessorEditor (GentraAudioProcessor& p)
    : AudioProcessorEditor (&p), gentraProcessor (p), road (p.apvts, [&p] { return p.takeBassLevel(); }, [&p] (double& beat, double& bpm) { return p.beatNow (beat, bpm); })
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.addAndMakeVisible (road);
    road.onSelectionChanged = [this] (int car) { showPopup (car); };

    // Всплывающая коробка открытой машины.
    // Тумблер: «выкл» в обычном состоянии, «вкл» — во включённом (ImageButton берёт down-картинку).
    const auto off = RoadView::image ("switch_off_png"), on = RoadView::image ("switch_on_png");
    const auto shade = juce::Colours::black.withAlpha (0.15f);
    onButton.setClickingTogglesState (true);
    onButton.setImages (false, true, true, off, 1.0f, {}, off, 1.0f, shade, on, 1.0f, {});
    const auto close = RoadView::image ("close_png");
    closeButton.setImages (false, true, true, close, 1.0f, {}, close, 1.0f, shade, close, 1.0f, juce::Colours::black.withAlpha (0.3f));
    onButton.setTitle (ru ("Включен"));
    closeButton.setTitle (ru ("Закрыть"));
    closeButton.onClick = [this] { road.select (-1); };
    popup.addAndMakeVisible (onButton);
    popup.addAndMakeVisible (closeButton);

    for (auto& row : rows)
    {
        row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
        row.slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::black);
        row.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        row.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, true, 62, 20);
        popup.addAndMakeVisible (row.label);
        popup.addAndMakeVisible (row.slider);
    }
    content.addChildComponent (popup);

    // Гараж: кнопка в углу открывает табличку со всеми машинами; клик по машине — скрыть/вернуть.
    garageButton.setButtonText (ru ("Гараж"));
    garageButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    garageButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    garageButton.onClick = [this] { showGarage (! garage.isVisible()); };
    content.addAndMakeVisible (garageButton);

    garageTitle.setText (ru ("Гараж — кто едет по дороге"), juce::dontSendNotification);
    garageTitle.setFont (RoadView::font (19.0f));
    garage.addAndMakeVisible (garageTitle);
    garageClose.setImages (false, true, true, close, 1.0f, {}, close, 1.0f, shade, close, 1.0f, juce::Colours::black.withAlpha (0.3f));
    garageClose.setTitle (ru ("Закрыть гараж"));
    garageClose.onClick = [this] { showGarage (false); };
    garage.addAndMakeVisible (garageClose);
    for (int i = 0; i < RoadView::numCars; ++i)
    {
        auto& chip = chips[static_cast<size_t> (i)];
        chip.road = &road;
        chip.car = i;
        chip.setTitle (RoadView::effectName (i));
        chip.onClick = [this, i] { road.setHidden (i, ! chips[static_cast<size_t> (i)].getToggleState()); };
        garage.addAndMakeVisible (chip);
    }
    content.addChildComponent (garage);
    layoutContent();

    // Масштаб окна, как у FabFilter: тянется за угол с сохранением пропорций; размер один на все проекты —
    // в настройках пользователя, не в пресете. Читаем до setResizable: тот уже зовёт resized() и перезаписал бы его.
    const double scale = juce::jlimit (minScale, maxScale, settings.getDoubleValue (uiScaleKey, 1.0));
    const double w = RoadView::width, h = RoadView::height;
    setResizable (true, true);
    setResizeLimits (juce::roundToInt (w * minScale), juce::roundToInt (h * minScale),
                     juce::roundToInt (w * maxScale), juce::roundToInt (h * maxScale));
    getConstrainer()->setFixedAspectRatio (w / h);
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
}

GentraAudioProcessorEditor::~GentraAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void GentraAudioProcessorEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (RoadView::width);
    content.setTransform (juce::AffineTransform::scale (scale));
    settings.setValue (uiScaleKey, scale);
}

juce::File GentraAudioProcessorEditor::settingsFile = [] {
    juce::PropertiesFile::Options o;
    o.applicationName = JucePlugin_Name;
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    return o.getDefaultFile();
}();

juce::PropertiesFile::Options GentraAudioProcessorEditor::settingsOptions()
{
    settingsFile.getParentDirectory().createDirectory(); // PropertiesFile сам папку не создаёт
    juce::PropertiesFile::Options o;
    o.millisecondsBeforeSaving = 500; // тянут за угол — пишем на диск, когда отпустили
    return o;
}

void GentraAudioProcessorEditor::layoutContent()
{
    content.setBounds (0, 0, RoadView::width, RoadView::height);
    road.setBounds (content.getLocalBounds());

    popup.setTransform ({});
    // Пропорции — как у panel.png (760×213); винты в углах, поэтому поля по 34.
    popup.setBounds ((RoadView::width - 380) / 2, RoadView::height - 8 - 106, 380, 106);
    onButton.setBounds (34, 12, 151, 30);
    closeButton.setBounds (380 - 34 - 36, 12, 36, 30);
    for (int i = 0; i < 2; ++i)
    {
        auto& row = rows[static_cast<size_t> (i)];
        const int y = 48 + i * 24;
        row.label.setBounds (34, y, 88, 22);
        row.slider.setBounds (122, y, 224, 22);
    }
    rotate (popup, -0.6f);

    garageButton.setTransform ({});
    garageButton.setBounds (RoadView::width - 8 - 96, 8, 96, 34);
    rotate (garageButton, 1.2f);

    // 3 × 4 машины по 110×58.
    constexpr int gw = 404, gh = 316, cw = 110, ch = 58, gap = 6;
    garage.setBounds ((RoadView::width - gw) / 2, (RoadView::height - gh) / 2, gw, gh);
    garageTitle.setBounds (30, 12, 300, 30);
    garageClose.setBounds (gw - 35 - 36, 12, 36, 30);
    for (int i = 0; i < RoadView::numCars; ++i)
        chips[static_cast<size_t> (i)].setBounds ((gw - 3 * cw - 2 * gap) / 2 + (i % 3) * (cw + gap), 50 + (i / 3) * (ch + 4), cw, ch);
}

void GentraAudioProcessorEditor::showGarage (bool show)
{
    for (int i = 0; i < RoadView::numCars; ++i) // маска могла смениться из пресета
        chips[static_cast<size_t> (i)].setToggleState (! road.isHidden (i), juce::dontSendNotification);
    garage.setVisible (show);
}

void GentraAudioProcessorEditor::showPopup (int car)
{
    onAttachment.reset();
    for (auto& row : rows)
        row.attachment.reset();

    popup.setVisible (car >= 0);
    if (car < 0)
        return;

    auto& apvts = gentraProcessor.apvts;
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, RoadView::working[static_cast<size_t> (car)].on, onButton);

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& extra = extras[static_cast<size_t> (car)][i];
        auto& row = rows[i];
        row.label.setText (ru (extra.label), juce::dontSendNotification);
        row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, extra.id, row.slider);
        // Attachment ставит свой форматтер — подменяем на русские единицы.
        row.slider.textFromValueFunction = [extra] (double v) { return juce::String (v, extra.decimals) + " " + ru (extra.unit); };
        row.slider.setTitle (ru (extra.label));
        row.slider.updateText();
    }
}
