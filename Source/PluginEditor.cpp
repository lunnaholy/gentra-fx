#include "PluginEditor.h"

namespace
{
const juce::Colour boxGrey { 0xffd9d9d9 };
const juce::Colour hot { 0xffffff00 };
const juce::Colour yes { 0xff35c435 };
const juce::Colour no { 0xffe23b3b };
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
    setColour (juce::TextButton::buttonColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonOnColourId, juce::Colours::white);
    setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Label::textColourId, juce::Colours::black);
    setColour (juce::Slider::backgroundColourId, juce::Colours::white);
    setColour (juce::Slider::trackColourId, juce::Colours::black);
    setColour (juce::Slider::thumbColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::black);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
}

void BoxLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& colour, bool, bool down)
{
    const auto r = b.getLocalBounds().toFloat();
    g.setColour (down ? hot : colour);
    g.fillRect (r);
    g.setColour (b.isEnabled() ? juce::Colours::black : juce::Colours::grey);
    g.drawRect (r, 2.0f);
}

juce::Font BoxLook::getTextButtonFont (juce::TextButton&, int)
{
    return juce::FontOptions ("Arial", 14.0f, juce::Font::bold);
}

juce::Font BoxLook::getLabelFont (juce::Label&)
{
    return juce::FontOptions ("Arial", 13.0f, juce::Font::bold);
}

void Box::paint (juce::Graphics& g)
{
    g.fillAll (boxGrey);
    g.setColour (juce::Colours::black);
    g.drawRect (getLocalBounds(), 3);
}

// ---------------- редактор ----------------

GentraAudioProcessorEditor::GentraAudioProcessorEditor (GentraAudioProcessor& p)
    : AudioProcessorEditor (&p), gentraProcessor (p), road (p.apvts, [&p] { return p.takeBassLevel(); })
{
    setLookAndFeel (&look);
    addAndMakeVisible (road);
    road.onSelectionChanged = [this] (int car) { showPopup (car); };

    // Всплывающая коробка открытой машины.
    onButton.setClickingTogglesState (true);
    onButton.setColour (juce::TextButton::buttonColourId, no);
    onButton.setColour (juce::TextButton::buttonOnColourId, yes);
    onButton.onStateChange = [this] { updateOnButton(); };
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
    popup.setBounds ((getWidth() - 300) / 2, getHeight() - 9 - 96, 300, 96);
    onButton.setBounds (8, 8, 244, 28);
    closeButton.setBounds (258, 8, 34, 28);
    for (int i = 0; i < 2; ++i)
    {
        auto& row = rows[static_cast<size_t> (i)];
        const int y = 42 + i * 25;
        row.label.setBounds (6, y, 86, 22);
        row.slider.setBounds (92, y, 202, 22);
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
    updateOnButton();

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

void GentraAudioProcessorEditor::updateOnButton()
{
    onButton.setButtonText (onButton.getToggleState() ? ru ("Включен") : ru ("Выключен"));
    onButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}
