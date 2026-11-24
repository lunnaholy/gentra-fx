#pragma once

#include <array>

#include "PluginProcessor.h"

class GentraAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit GentraAudioProcessorEditor (GentraAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Один шаг анимации. Публичный, чтобы тест мог прокрутить сцену и снять кадр без окна.
    void advance (float dt);

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct TrafficCar
    {
        float x;   // левый край, px
        int lane;  // 0 — дальняя полоса, 1 — полоса джентры
        int skin;  // индекс картинки
    };

    void timerCallback() override;

    float wheelRadiusPx() const;
    juce::Rectangle<float> bodyBounds (float baselineY) const;
    juce::Point<float> wheelCentre (juce::Rectangle<float> bounds, float xFraction) const;
    float trafficWidth (const TrafficCar&) const;

    void drawScenery (juce::Graphics&) const;
    void drawTraffic (juce::Graphics&, int lane) const;
    void drawKnob (juce::Graphics&, juce::Point<float> centre, const juce::Slider&, const juce::String& title) const;

    juce::Image body, wheel;
    std::array<juce::Image, 3> traffic;

    // Слайдеры невидимы и неподвижны: это зоны захвата поверх колёс, рисуем всё сами.
    juce::Slider bassSlider, loresSlider;
    SliderAttachment bassAttachment, loresAttachment;

    std::array<TrafficCar, 4> cars;
    juce::Random rng;
    double distance = 0.0;   // пройденный путь, px
    double lastTime = 0.0;
    float wheelAngle = 0.0f;
    float laneBlend = 0.0f;  // 0 — своя полоса, 1 — ушла на ближнюю (обгон)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessorEditor)
};
