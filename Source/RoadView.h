#pragma once

#include <array>
#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

// Дорога с машинами — порт HTML-прототипа. Машины 0 (чёрная) и 1 (ржавая) — рабочие эффекты:
// клик по машине приближает камеру, и её колёса становятся ручками. Остальные едут декоративным потоком.
class RoadView final : public juce::Component,
                       private juce::Timer
{
public:
    static constexpr int numCars = 12;
    static constexpr int numWorking = 2;
    static constexpr int width = 900, height = 560;

    struct Working
    {
        const char* main;      // параметр заднего колеса
        const char* mainLabel; // подпись под ним, заглавными: JUCE не меняет регистр кириллицы
        const char* mix;       // параметр переднего колеса
        const char* on;        // вкл/выкл
    };
    static const std::array<Working, numWorking> working;

    static juce::String effectName (int car);
    static juce::Colour bodyColour (int car);
    static juce::Image image (const char* binaryDataName); // PNG из Assets/
    static juce::Font font (float size);                   // шрифт интерфейса

    // bassLevel — откуда брать уровень низа для тряски; beatClock — где сейчас транспорт DAW
    // (доля в такте и темп в долях в минуту, false — стоит). Оба зовутся раз в кадр на message thread.
    using BeatClock = std::function<bool (double& beatInBar, double& bpm)>;
    RoadView (juce::AudioProcessorValueTreeState&, std::function<float()> bassLevel, BeatClock beatClock = {});

    // Скрытые машины не рисуются и не мешают потоку; скрытый рабочий эффект выключается.
    // Маска хранится в состоянии плагина (пресеты).
    bool isHidden (int car) const;
    void setHidden (int car, bool hidden);
    void drawThumbnail (juce::Graphics&, int car, juce::Rectangle<float> area, float alpha) const;

    void select (int car); // -1 — ничего не выбрано
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onSelectionChanged;

    void advance (float dt); // один шаг анимации; публичный, чтобы тест мог прокрутить сцену

    // Для теста шашек.
    int getLaneChanges() const noexcept { return laneChanges; }
    float closestGap() const; // минимальная дистанция между машинами в одной полосе
    float getRumble() const noexcept { return rumble; } // сила тряски в этом кадре, 0..1
    int flashingCars() const; // сколько машин сейчас моргают дальним

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Car
    {
        float x = 0.0f, lp = 0.0f, ve = 0.3f, cool = 0.0f, spin = 0.0f;
        float waiting = 0.0f; // сколько секунд упирается в медленного и не может объехать
        float px = 0.0f, py = 0.0f, sc = 1.0f; // положение на экране без учёта камеры
        int lane = 0;
    };

    void timerCallback() override;

    float param (const char* id) const;
    bool isOn (int car) const;
    float strength (int car) const;
    bool laneFree (int car, int lane) const;
    float room (int car, int lane) const;
    void weave (int car);
    void place();

    juce::Point<float> toScreen (float worldX, float worldY) const;
    juce::Point<float> wheelCentre (int wheel) const;
    float wheelHitRadius() const;
    bool knobsVisible() const;
    int carAt (juce::Point<float>) const;
    int wheelAt (juce::Point<float>) const;
    juce::RangedAudioParameter* knobParam (int wheel) const;

    void drawScene (juce::Graphics&) const;
    void drawCar (juce::Graphics&, int car, float px) const; // px — где рисовать (копия у края кольца)
    void drawOverlays (juce::Graphics&) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::function<float()> bassLevel;
    BeatClock beatClock;

    std::array<juce::Image, numCars> bodies;
    juce::Image sky, mountains, skyline, road, lamp, props, wheel, tint, rust, plate, tag;

    std::array<Car, numCars> cars;
    juce::Random rng;
    int selected = -1, hovered = -1;
    float time = 0.0f, scroll = 0.0f, camX, camU = 0.0f;
    float focus = 0.0f, zoom = 1.0f, zoomGoal = 1.0f, focusY, centreY, lastFocusY;
    double lastTick = 0.0;
    int laneChanges = 0;
    int hiddenMask = 0; // копия маски из состояния на этот кадр
    bool hidden (int car) const noexcept { return (hiddenMask >> car) & 1; }
    float rumble = 0.0f;            // сила тряски в этом кадре
    float bassHold = 0.0f;          // уровень низа, удерживаемый между долями
    float pulse = 0.0f;             // фаза доли 0..1 (на стопе — свои 2 Гц): саб, аварийка
    juce::Point<float> shake;       // смещение земли и машин в этом кадре

    struct Drag
    {
        int wheel = -1;
        float angle = 0.0f, value = 0.0f;
    } drag;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoadView)
};
