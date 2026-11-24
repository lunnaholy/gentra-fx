#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff15151a };
const juce::Colour track { 0xff2c2c36 };
const juce::Colour accent { 0xffe8a33d };
const juce::Colour text { 0xffeeeeee };
}

GentraAudioProcessorEditor::GentraAudioProcessorEditor (GentraAudioProcessor& p)
    : AudioProcessorEditor (&p),
      bassAttachment (p.apvts, "bass", bassSlider),
      loresAttachment (p.apvts, "lores", loresSlider)
{
    setupKnob (bassSlider, bassLabel, "BASS BOOST");
    setupKnob (loresSlider, loresLabel, "LO-RES");
    setSize (420, 260);
}

void GentraAudioProcessorEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& title)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 24);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, track);
    slider.setColour (juce::Slider::thumbColourId, text);
    slider.setColour (juce::Slider::textBoxTextColourId, text);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    // Attachment ставит свой форматтер без единиц — подменяем на «37 %».
    slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    slider.updateText();
    addAndMakeVisible (slider);

    label.setText (title, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, text);
    addAndMakeVisible (label);
}

void GentraAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("SAUCE GENTRA FX", getLocalBounds().removeFromTop (36), juce::Justification::centred);
}

void GentraAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (28);

    auto left = area.removeFromLeft (area.getWidth() / 2);
    bassLabel.setBounds (left.removeFromTop (24));
    bassSlider.setBounds (left);

    loresLabel.setBounds (area.removeFromTop (24));
    loresSlider.setBounds (area);
}
