#include "PluginEditor.h"

#include "BinaryData.h"

namespace
{
// ====================== ГРАФИКА: КАК ЗАМЕНИТЬ ======================
// Картинки лежат в Assets/ и вшиваются в плагин при сборке. Чтобы поставить свои фото,
// замените файлы с теми же именами (PNG с прозрачным фоном) и пересоберите:
//   body.png      — кузов, вид сбоку, нос вправо. Колёса на фото можно оставить — их перекроет wheel.png.
//   wheel.png     — колесо строго анфас, квадратная картинка, колесо вписано в края.
//   traffic1..3   — машины потока, вид сбоку, нос вправо, низ картинки = низ колёс.
// Затем подгоните четыре числа ниже под своё фото кузова:
constexpr float rearWheelX   = 0.2098f; // центр заднего колеса: доля ширины body.png
constexpr float frontWheelX  = 0.7857f; // центр переднего колеса: доля ширины body.png
constexpr float wheelCentreY = 0.7789f; // центр колёс: доля высоты body.png
constexpr float wheelRadius  = 0.0679f; // радиус колеса: доля ширины body.png
// Проверить подгонку без DAW: build/GentraTest --snapshot кадр.png
// ===================================================================

constexpr int windowW = 760, windowH = 440;
constexpr float carX = 100.0f, carWidth = 560.0f; // джентра на экране
constexpr float trafficBaseWidth = 470.0f, farScale = 0.85f;
constexpr float horizonY = 170.0f, roadTopY = 190.0f;
constexpr float laneFarY = 262.0f, laneMidY = 318.0f, laneNearY = 372.0f; // где колёса касаются дороги
constexpr float cruiseSpeed = 260.0f;   // px/с при ручках на нуле
constexpr float speedBoost = 3.0f;      // на 100% едем в (1 + speedBoost) раз быстрее
constexpr float overtakeSpeed = 520.0f; // px/с относительно потока при 100%

const juce::Colour accent { 0xffe8a33d };
const juce::Colour text { 0xffeeeeee };

juce::Image loadImage (const char* data, int size)
{
    return juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size));
}

float wrap (double value, float period)
{
    return static_cast<float> (std::fmod (value, static_cast<double> (period)));
}
}

GentraAudioProcessorEditor::GentraAudioProcessorEditor (GentraAudioProcessor& p)
    : AudioProcessorEditor (&p),
      body (loadImage (BinaryData::body_png, BinaryData::body_pngSize)),
      wheel (loadImage (BinaryData::wheel_png, BinaryData::wheel_pngSize)),
      traffic { { loadImage (BinaryData::traffic1_png, BinaryData::traffic1_pngSize),
                  loadImage (BinaryData::traffic2_png, BinaryData::traffic2_pngSize),
                  loadImage (BinaryData::traffic3_png, BinaryData::traffic3_pngSize) } },
      bassAttachment (p.apvts, "bass", bassSlider),
      loresAttachment (p.apvts, "lores", loresSlider),
      // На старте в полосе джентры пусто: при ручках на нуле она просто едет в потоке.
      cars { { { 430.0f, 0, 0 }, { -260.0f, 0, 1 }, { 820.0f, 1, 2 }, { 1500.0f, 0, 1 } } }
{
    jassert (body.isValid() && wheel.isValid());

    bassSlider.setTitle ("Bass Boost");
    loresSlider.setTitle ("Lo-Res");

    for (auto* slider : { &bassSlider, &loresSlider })
    {
        slider->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        for (int id : { juce::Slider::rotarySliderFillColourId, juce::Slider::rotarySliderOutlineColourId, juce::Slider::thumbColourId })
            slider->setColour (id, juce::Colours::transparentBlack);
        addAndMakeVisible (*slider);
    }

    setOpaque (true);
    setSize (windowW, windowH);

    lastTime = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

float GentraAudioProcessorEditor::wheelRadiusPx() const
{
    return wheelRadius * carWidth;
}

juce::Rectangle<float> GentraAudioProcessorEditor::bodyBounds (float baselineY) const
{
    const float height = carWidth * static_cast<float> (body.getHeight()) / static_cast<float> (juce::jmax (1, body.getWidth()));
    return { carX, baselineY - (wheelCentreY * height + wheelRadiusPx()), carWidth, height };
}

juce::Point<float> GentraAudioProcessorEditor::wheelCentre (juce::Rectangle<float> bounds, float xFraction) const
{
    return { bounds.getX() + xFraction * bounds.getWidth(), bounds.getY() + wheelCentreY * bounds.getHeight() };
}

float GentraAudioProcessorEditor::trafficWidth (const TrafficCar& car) const
{
    return trafficBaseWidth * (car.lane == 0 ? farScale : 1.0f);
}

void GentraAudioProcessorEditor::resized()
{
    // Зона захвата накрывает колесо в обеих полосах, чтобы ручка не «убегала» из-под мыши.
    const auto bounds = bodyBounds (laneMidY);
    const float r = wheelRadiusPx();

    const auto hitArea = [&] (float xFraction)
    {
        const auto c = wheelCentre (bounds, xFraction);
        return juce::Rectangle<float> (c.x - r, c.y - r, 2.0f * r, 2.0f * r + laneNearY - laneMidY).toNearestInt();
    };

    bassSlider.setBounds (hitArea (rearWheelX));
    loresSlider.setBounds (hitArea (frontWheelX));
}

void GentraAudioProcessorEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    advance (juce::jlimit (0.0f, 0.05f, static_cast<float> ((now - lastTime) * 0.001)));
    lastTime = now;
}

void GentraAudioProcessorEditor::advance (float dt)
{
    const auto amount = static_cast<float> ((bassSlider.getValue() + loresSlider.getValue()) / 200.0);
    const float roadSpeed = cruiseSpeed * (1.0f + speedBoost * amount);
    const float overtake = overtakeSpeed * amount;

    distance += static_cast<double> (roadSpeed * dt);
    wheelAngle = std::fmod (wheelAngle + roadSpeed * dt / wheelRadiusPx(), juce::MathConstants<float>::twoPi);

    // Поток: машины уезжают назад, за левым краем перерождаются справа.
    bool blocked = false;
    const float lookAhead = 60.0f + overtake * 0.35f; // чем быстрее, тем раньше перестраиваемся

    for (auto& car : cars)
    {
        car.x -= overtake * dt;

        if (car.x + trafficWidth (car) < -20.0f)
        {
            car.lane = rng.nextInt (2);
            car.skin = rng.nextInt (static_cast<int> (traffic.size()));

            float spawnX = static_cast<float> (windowW) + 40.0f;
            for (const auto& other : cars)
                if (&other != &car && other.lane == car.lane)
                    spawnX = juce::jmax (spawnX, other.x + trafficWidth (other) + 120.0f);

            car.x = spawnX + rng.nextFloat() * 320.0f;
        }

        if (car.lane == 1 && car.x < carX + carWidth + lookAhead && car.x + trafficWidth (car) > carX - 40.0f)
            blocked = true;
    }

    laneBlend += ((blocked ? 1.0f : 0.0f) - laneBlend) * (1.0f - std::exp (-dt * 9.0f));
    repaint();
}

void GentraAudioProcessorEditor::drawScenery (juce::Graphics& g) const
{
    const auto width = static_cast<float> (getWidth());
    const auto height = static_cast<float> (getHeight());

    // Небо
    g.setGradientFill ({ juce::Colour (0xff1b2444), 0.0f, 0.0f, juce::Colour (0xffd0773f), 0.0f, horizonY, false });
    g.fillRect (0.0f, 0.0f, width, horizonY);

    // Город на горизонте (едет медленно — параллакс)
    constexpr float buildingW = 64.0f;
    const double cityOffset = distance * 0.06;
    const auto firstBuilding = static_cast<juce::int64> (cityOffset / static_cast<double> (buildingW));
    g.setColour (juce::Colour (0xff161a2e));

    for (juce::int64 i = firstBuilding; i < firstBuilding + 14; ++i)
    {
        const auto hash = static_cast<juce::uint32> (i) * 2654435761u;
        const auto h = 24.0f + static_cast<float> ((hash >> 20) % 80u);
        const auto x = static_cast<float> (static_cast<double> (i) * static_cast<double> (buildingW) - cityOffset);
        g.fillRect (x, horizonY - h, buildingW - 6.0f, h);
    }

    // Обочина и отбойник
    g.setColour (juce::Colour (0xff24252c));
    g.fillRect (0.0f, horizonY, width, roadTopY - horizonY);
    g.setColour (juce::Colour (0xff8b8f99));
    g.fillRect (0.0f, horizonY + 6.0f, width, 3.0f);
    for (float x = -wrap (distance * 0.9, 48.0f); x < width; x += 48.0f)
        g.fillRect (x, horizonY + 6.0f, 3.0f, 12.0f);

    // Асфальт и разметка
    g.setColour (juce::Colour (0xff3a3b43));
    g.fillRect (0.0f, roadTopY, width, height - roadTopY);
    g.setColour (juce::Colour (0xffd8d8d8));
    g.fillRect (0.0f, roadTopY + 8.0f, width, 3.0f);

    for (float y : { (laneFarY + laneMidY) * 0.5f - 12.0f, (laneMidY + laneNearY) * 0.5f - 10.0f })
        for (float x = -wrap (distance, 110.0f); x < width; x += 110.0f)
            g.fillRect (x, y, 54.0f, 4.0f);
}

void GentraAudioProcessorEditor::drawTraffic (juce::Graphics& g, int lane) const
{
    for (const auto& car : cars)
    {
        if (car.lane != lane)
            continue;

        const auto& image = traffic[static_cast<size_t> (car.skin)];
        const float w = trafficWidth (car);
        const float h = w * static_cast<float> (image.getHeight()) / static_cast<float> (juce::jmax (1, image.getWidth()));
        const float baseline = lane == 0 ? laneFarY : laneMidY;

        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillEllipse (car.x + w * 0.03f, baseline - 7.0f, w * 0.94f, 14.0f);
        g.setOpacity (1.0f); // картинки рисуются с альфой текущего цвета
        g.drawImage (image, { car.x, baseline - h, w, h });
    }
}

void GentraAudioProcessorEditor::drawKnob (juce::Graphics& g, juce::Point<float> centre, const juce::Slider& slider, const juce::String& title) const
{
    const float r = wheelRadiusPx();

    // Колесо катится со скоростью дороги
    const auto w = static_cast<float> (juce::jmax (1, wheel.getWidth()));
    const auto h = static_cast<float> (juce::jmax (1, wheel.getHeight()));
    g.setOpacity (1.0f);
    g.drawImageTransformed (wheel, juce::AffineTransform::scale (2.0f * r / w, 2.0f * r / h)
                                       .translated (centre.x - r, centre.y - r)
                                       .rotated (wheelAngle, centre.x, centre.y));

    // Значение — дугой вокруг колеса
    const auto value = static_cast<float> (slider.getValue() / slider.getMaximum());
    const float start = -0.75f * juce::MathConstants<float>::pi;
    const float sweep = 1.5f * juce::MathConstants<float>::pi;
    const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path trackArc, valueArc;
    trackArc.addCentredArc (centre.x, centre.y, r + 7.0f, r + 7.0f, 0.0f, start, start + sweep, true);
    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.strokePath (trackArc, stroke);

    if (value > 0.0f)
    {
        valueArc.addCentredArc (centre.x, centre.y, r + 7.0f, r + 7.0f, 0.0f, start, start + sweep * value, true);
        g.setColour (accent);
        g.strokePath (valueArc, stroke);
    }

    // Подписи стоят на месте, под колесом
    const juce::Rectangle<float> labelArea (centre.x - 90.0f, static_cast<float> (getHeight()) - 48.0f, 180.0f, 20.0f);
    g.setColour (text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, labelArea, juce::Justification::centred);
    g.setColour (accent);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (slider.getValue())) + " %", labelArea.translated (0.0f, 20.0f), juce::Justification::centred);
}

void GentraAudioProcessorEditor::paint (juce::Graphics& g)
{
    drawScenery (g);

    g.setColour (text.withAlpha (0.85f));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("SAUCE GENTRA FX", getLocalBounds().removeFromTop (30).reduced (12, 0), juce::Justification::centredLeft);

    drawTraffic (g, 0);
    drawTraffic (g, 1);

    // Джентра: всегда ближе к камере, чем поток, поэтому её ничто не перекрывает.
    const float baseline = laneMidY + (laneNearY - laneMidY) * laneBlend;
    const auto bounds = bodyBounds (baseline);

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (bounds.getX() + 12.0f, baseline - 8.0f, bounds.getWidth() - 24.0f, 16.0f);
    g.setOpacity (1.0f);
    g.drawImage (body, bounds);

    drawKnob (g, wheelCentre (bounds, rearWheelX), bassSlider, "BASS BOOST");
    drawKnob (g, wheelCentre (bounds, frontWheelX), loresSlider, "LO-RES");
}
