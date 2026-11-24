#include "PluginEditor.h"

namespace
{
constexpr float degrees = juce::MathConstants<float>::pi / 180.0f;

juce::String ru (const char* s) { return juce::String::fromUTF8 (s); }

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

void rotate (juce::Component& c, float deg)
{
    c.setTransform (juce::AffineTransform::rotation (deg * degrees, c.getBounds().toFloat().getCentreX(), c.getBounds().toFloat().getCentreY()));
}
}

// ---------------- стиль ----------------

BoxLook::BoxLook()
{
    setColour (juce::Label::textColourId, juce::Colours::black);
    setColour (juce::Slider::backgroundColourId, juce::Colours::white);
    setColour (juce::Slider::trackColourId, juce::Colours::black);
    setColour (juce::Slider::thumbColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

juce::Font BoxLook::getLabelFont (juce::Label&)
{
    return RoadView::font (16.0f);
}

void Box::paint (juce::Graphics& g)
{
    g.drawImage (image, getLocalBounds().toFloat());
}

// ---------------- редактор ----------------

GentraAudioProcessorEditor::GentraAudioProcessorEditor (GentraAudioProcessor& p)
    : AudioProcessorEditor (&p), gentraProcessor (p), road (p.apvts, [&p] { return p.takeBassLevel(); })
{
    setLookAndFeel (&look);
    addAndMakeVisible (road);
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
    addChildComponent (popup);

    setSize (RoadView::width, RoadView::height);
}

GentraAudioProcessorEditor::~GentraAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void GentraAudioProcessorEditor::resized()
{
    road.setBounds (getLocalBounds());

    popup.setTransform ({});
    // Пропорции — как у panel.png (760×213); винты в углах, поэтому поля по 34.
    popup.setBounds ((getWidth() - 380) / 2, getHeight() - 8 - 106, 380, 106);
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
