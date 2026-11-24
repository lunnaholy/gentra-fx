#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace
{
constexpr double smoothingSeconds = 0.05;
}

GentraAudioProcessor::GentraAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    bassParam = apvts.getRawParameterValue ("bass");
    loresParam = apvts.getRawParameterValue ("lores");
}

juce::AudioProcessorValueTreeState::ParameterLayout GentraAudioProcessor::createParameterLayout()
{
    const juce::NormalisableRange<float> percent (0.0f, 100.0f, 0.1f);
    const auto attributes = juce::AudioParameterFloatAttributes().withLabel ("%");

    return {
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bass", 1 }, "Bass Boost", percent, 0.0f, attributes),
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "lores", 1 }, "Lo-Res", percent, 0.0f, attributes)
    };
}

void GentraAudioProcessor::prepareToPlay (double sampleRate, int)
{
    bass.prepare (sampleRate);
    lores.prepare (sampleRate);

    bassSmoothed.reset (sampleRate, smoothingSeconds);
    loresSmoothed.reset (sampleRate, smoothingSeconds);
    bassSmoothed.setCurrentAndTargetValue (bassParam->load() * 0.01f);
    loresSmoothed.setCurrentAndTargetValue (loresParam->load() * 0.01f);
}

void GentraAudioProcessor::reset()
{
    bass.reset();
    lores.reset();
    bassSmoothed.setCurrentAndTargetValue (bassParam->load() * 0.01f);
    loresSmoothed.setCurrentAndTargetValue (loresParam->load() * 0.01f);
}

bool GentraAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void GentraAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    bassSmoothed.setTargetValue (bassParam->load() * 0.01f);
    loresSmoothed.setTargetValue (loresParam->load() * 0.01f);

    // Стадия с ручкой на нуле пропускается целиком: это и даёт bit-perfect при 0%.
    const bool bassOn = bassSmoothed.isSmoothing() || bassSmoothed.getTargetValue() > 0.0f;
    const bool loresOn = loresSmoothed.isSmoothing() || loresSmoothed.getTargetValue() > 0.0f;

    if (! bassOn)
        bass.reset();
    if (! loresOn)
        lores.reset();
    if (! bassOn && ! loresOn)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), gentra::maxChannels);
    auto* const* data = buffer.getArrayOfWritePointers();

    for (int n = 0; n < numSamples; ++n)
    {
        if (bassOn)
        {
            bass.setAmount (bassSmoothed.getNextValue());
            for (int ch = 0; ch < numChannels; ++ch)
                data[ch][n] = bass.processSample (ch, data[ch][n]);
        }

        if (loresOn)
        {
            lores.setAmount (loresSmoothed.getNextValue());
            const bool capture = lores.tick();
            for (int ch = 0; ch < numChannels; ++ch)
                data[ch][n] = lores.processSample (ch, data[ch][n], capture);
        }
    }
}

juce::AudioProcessorEditor* GentraAudioProcessor::createEditor()
{
    return new GentraAudioProcessorEditor (*this);
}

void GentraAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (const auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GentraAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GentraAudioProcessor();
}
