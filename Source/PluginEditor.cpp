#include "PluginEditor.h"

namespace
{
constexpr float degrees = juce::MathConstants<float>::pi / 180.0f;

juce::String ru (const char* s) { return juce::String::fromUTF8 (s); }

constexpr double minScale = 0.6, maxScale = 2.0;
const juce::String uiScaleKey { "uiScale" };

// Доп. параметры машины во всплывающей коробке (главная ручка и Mix — на колёсах). id nullptr — строки нет.
struct Extra
{
    const char* id = nullptr;
    const char* label = "";
    const char* unit = "";
    int decimals = 0;
    bool sign = false; // «+» у положительных (параметр от минуса до плюса)
};
// Номера машин — gentra::car: 0 чёрная, 1 ржавая, 2 красная, 3 зелёная, 5 фиолетовая, 6 синяя, 7 розовая, 8 серебряная, 9 серая, 10 жёлтая, 11 белая.
const std::array<std::array<Extra, 3>, RoadView::numCars> extras = []
{
    std::array<std::array<Extra, 3>, RoadView::numCars> e {};
    e[0] = { { { "bassFreq", "Частота", "Гц", 0 }, { "tint", "Тонировка", "%", 0 }, {} } };
    e[1] = { { { "loresBits", "Мин. биты", "бит", 1 }, { "loresRate", "Мин. частота", "кГц", 1 }, {} } };
    e[2] = { { { "distTone", "Тон", "%", 0 }, {}, {} } };
    e[3] = { { { "tapeEvery", "Каждые", "", 0 }, { "tapeSpin", "Разгон", "%", 0 }, {} } };
    e[5] = { { { "sidechainNote", "Шаг", "", 0 }, { "sidechainShape", "Форма", "%", 0 }, {} } };
    e[6] = { { { "chorusRate", "Скорость", "Гц", 2 }, { "chorusFeedback", "Возврат", "%", 0 }, {} } };
    e[7] = { { { "phaserRate", "Скорость", "Гц", 2 }, { "phaserFeedback", "Возврат", "%", 0 }, {} } };
    e[8] = { { { "delayFeedback", "Повторы", "%", 0 }, { "delayTone", "Тон", "%", 0 }, {} } };
    e[9] = { { { "reverbDamping", "Глушение", "%", 0 }, {}, {} } };
    e[10] = { { { "gateNote", "Шаг", "", 0 }, { "gateSmooth", "Плавность", "%", 0 }, {} } };
    e[11] = { { { "eqLow", "Низ", "дБ", 1, true }, { "eqMid", "Середина", "дБ", 1, true }, { "eqHigh", "Верх", "дБ", 1, true } } };
    return e;
}();

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
    if (b.getToggleState()) // включённая табличка — подсвечена, как дуга ручки
    {
        g.setColour (juce::Colour (0xffe8a23a).withAlpha (0.55f));
        g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (3.0f), 4.0f);
    }
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

constexpr int thumbHalf = 12; // ручка ~24×22; ползунок ходит в пределах, где она целиком внутри паза

int BoxLook::getSliderThumbRadius (juce::Slider&)
{
    return thumbHalf;
}

void BoxLook::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                juce::Slider::SliderStyle, juce::Slider& slider)
{
    // x..x+width — ход центра ручки; паз шире на полручки с каждой стороны.
    const float cy = static_cast<float> (y) + static_cast<float> (height) * 0.5f;
    const auto groove = juce::Rectangle<float> (static_cast<float> (x - thumbHalf), cy - 8.0f,
                                                static_cast<float> (width + 2 * thumbHalf), 16.0f);
    g.setOpacity (1.0f);
    g.drawImage (track, groove);
    {
        juce::Graphics::ScopedSaveState s (g);
        // От минуса до плюса — светится от середины (ноль) до ручки, иначе — от начала.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float from = bipolar ? static_cast<float> (slider.getPositionOfValue (0.0)) : groove.getX();
        g.reduceClipRegion (groove.withLeft (juce::jmin (from, sliderPos)).withRight (juce::jmax (from, sliderPos)).getSmallestIntegerContainer());
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

// ---------------- график эквалайзера ----------------

namespace
{
struct Band
{
    const char* freq;
    const char* gain;
};
constexpr std::array<Band, 3> eqBands { { { "eqLowFreq", "eqLow" }, { "eqMidFreq", "eqMid" }, { "eqHighFreq", "eqHigh" } } };
constexpr double graphLoHz = 20.0, graphHiHz = 20000.0, graphDb = 15.0;
constexpr double graphFs = 96000.0; // АЧХ считается на высокой частоте: у 20 кГц кривая не загибается к Найквисту
constexpr float handleRadius = 5.0f, grabRadius = 11.0f;
}

EqGraph::EqGraph (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    curve.prepare (graphFs);
    shown = current();
    setTitle (ru ("График эквалайзера"));
    startTimerHz (30);
}

float EqGraph::value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

gentra::EqSettings EqGraph::current() const
{
    gentra::EqSettings s;
    s.volumeDb = value ("eqVolume");
    s.lowDb = value ("eqLow");
    s.midDb = value ("eqMid");
    s.highDb = value ("eqHigh");
    s.lowHz = value ("eqLowFreq");
    s.midHz = value ("eqMidFreq");
    s.highHz = value ("eqHighFreq");
    s.midQ = value ("eqMidQ");
    return s;
}

void EqGraph::set (const char* id, float v)
{
    auto* p = apvts.getParameter (id);
    const auto& range = p->getNormalisableRange();
    p->setValueNotifyingHost (range.convertTo0to1 (range.snapToLegalValue (juce::jlimit (range.start, range.end, v))));
}

void EqGraph::timerCallback()
{
    const auto now = current();
    if (isShowing() && (now.bandsDiffer (shown) || gentra::changed (now.volumeDb, shown.volumeDb)))
    {
        shown = now;
        repaint();
    }
}

juce::Rectangle<float> EqGraph::plot() const { return getLocalBounds().toFloat().reduced (6.0f, 5.0f); }

float EqGraph::xOf (double hz) const
{
    const auto r = plot();
    return r.getX() + r.getWidth() * static_cast<float> (std::log (hz / graphLoHz) / std::log (graphHiHz / graphLoHz));
}

double EqGraph::hzAt (float x) const
{
    const auto r = plot();
    return graphLoHz * std::pow (graphHiHz / graphLoHz, static_cast<double> ((x - r.getX()) / r.getWidth()));
}

float EqGraph::yOf (double db) const
{
    const auto r = plot();
    return r.getCentreY() - r.getHeight() * 0.5f * static_cast<float> (db / graphDb);
}

double EqGraph::dbAt (float y) const
{
    const auto r = plot();
    return static_cast<double> ((r.getCentreY() - y) / (r.getHeight() * 0.5f)) * graphDb;
}

juce::Point<float> EqGraph::handle (int band) const
{
    // Кривая включает громкость — точки едут вместе с ней.
    const auto& b = eqBands[static_cast<size_t> (band)];
    return { xOf (value (b.freq)), yOf (value (b.gain) + value ("eqVolume")) };
}

int EqGraph::bandAt (juce::Point<float> at) const
{
    int best = -1;
    float bestDistance = grabRadius;
    for (int band = 0; band < 3; ++band)
        if (const float d = handle (band).getDistanceFrom (at); d < bestDistance)
        {
            best = band;
            bestDistance = d;
        }
    return best;
}

void EqGraph::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    // Экран магнитолы: тёмное стекло в рамке.
    g.setColour (juce::Colour (0xff0d1013));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 6.0f, 1.0f);

    const auto r = plot();
    g.setFont (RoadView::font (10.0f));
    for (double hz : { 100.0, 1000.0, 10000.0 })
    {
        const float x = xOf (hz);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine (juce::roundToInt (x), r.getY(), r.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawText (hz < 1000.0 ? "100" : hz < 10000.0 ? "1k" : "10k", juce::Rectangle<float> (x + 2.0f, r.getBottom() - 11.0f, 24.0f, 11.0f),
                    juce::Justification::centredLeft);
    }
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawHorizontalLine (juce::roundToInt (yOf (0.0)), r.getX(), r.getRight());

    // Кривая: те же биквады, что в звуке.
    curve.reset();
    curve.setParams (shown);
    juce::Path line;
    for (float x = r.getX(); x <= r.getRight() + 0.5f; x += 1.5f)
    {
        const float y = juce::jlimit (r.getY(), r.getBottom(), yOf (curve.gainDbAt (hzAt (x))));
        if (x <= r.getX()) line.startNewSubPath (x, y);
        else line.lineTo (x, y);
    }
    juce::Path fill (line);
    fill.lineTo (r.getRight(), yOf (0.0));
    fill.lineTo (r.getX(), yOf (0.0));
    fill.closeSubPath();
    const juce::Colour amber (0xffe8a23a);
    g.setColour (amber.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (amber);
    g.strokePath (line, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (int band = 0; band < 3; ++band)
    {
        const auto p = handle (band);
        const bool active = band == dragging || band == hovered;
        g.setColour (active ? juce::Colours::white : amber);
        g.fillEllipse (p.x - handleRadius, p.y - handleRadius, 2.0f * handleRadius, 2.0f * handleRadius);
        g.setColour (juce::Colour (0xff0d1013));
        g.setFont (RoadView::font (10.0f));
        g.drawText (juce::String (band + 1), juce::Rectangle<float> (p.x - handleRadius, p.y - handleRadius, 2.0f * handleRadius, 2.0f * handleRadius),
                    juce::Justification::centred);
    }
}

void EqGraph::mouseMove (const juce::MouseEvent& e)
{
    const int band = bandAt (e.position);
    if (band != hovered)
    {
        hovered = band;
        setMouseCursor (band >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqGraph::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void EqGraph::mouseDown (const juce::MouseEvent& e)
{
    dragging = bandAt (e.position);
    if (dragging < 0)
        return;
    const auto& b = eqBands[static_cast<size_t> (dragging)];
    apvts.getParameter (b.freq)->beginChangeGesture();
    apvts.getParameter (b.gain)->beginChangeGesture();
}

void EqGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto& b = eqBands[static_cast<size_t> (dragging)];
    set (b.freq, static_cast<float> (hzAt (e.position.x)));
    set (b.gain, static_cast<float> (dbAt (e.position.y)) - value ("eqVolume"));
    timerCallback(); // точка едет за мышью без ожидания таймера
}

void EqGraph::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0)
        return;
    const auto& b = eqBands[static_cast<size_t> (dragging)];
    apvts.getParameter (b.freq)->endChangeGesture();
    apvts.getParameter (b.gain)->endChangeGesture();
    dragging = -1;
    repaint();
}

void EqGraph::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int band = bandAt (e.position); band >= 0)
        set (eqBands[static_cast<size_t> (band)].gain, 0.0f);
}

void EqGraph::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (bandAt (e.position) != 1) // ширина есть только у колокола
        return;
    set ("eqMidQ", value ("eqMidQ") * std::pow (1.5f, wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 4.0f));
}

// ---------------- гараж ----------------

namespace
{
constexpr float rowsTop = 62.0f, rowStep = 39.0f, rowHeight = 36.0f, rowInset = 16.0f;
constexpr juce::uint32 ink = 0xff15161a, accent = 0xffe8a23a;
}

GaragePanel::GaragePanel (GentraAudioProcessor& p, RoadView& r) : processor (p), road (r)
{
    close.setTitle (ru ("Закрыть гараж"));
    addAndMakeVisible (close);
    setTitle (ru ("Гараж"));
}

void GaragePanel::resized()
{
    close.setBounds (getWidth() - 24 - 36, 14, 36, 30);
}

juce::Rectangle<float> GaragePanel::rowBounds (int slot) const
{
    return { rowInset, rowsTop + rowStep * static_cast<float> (slot), static_cast<float> (getWidth()) - 2.0f * rowInset, rowHeight };
}

juce::Rectangle<float> GaragePanel::toggleBounds (juce::Rectangle<float> row) const
{
    return { row.getRight() - 46.0f, row.getCentreY() - 9.0f, 36.0f, 18.0f };
}

int GaragePanel::slotAt (float y) const
{
    const int slot = static_cast<int> (std::floor ((y - rowsTop) / rowStep));
    return slot >= 0 && slot < RoadView::numCars ? slot : -1;
}

void GaragePanel::paint (juce::Graphics& g)
{
    g.setOpacity (1.0f);
    drawNine (g, image, getLocalBounds().toFloat(), 70, 35.0f);

    g.setColour (juce::Colour (ink));
    g.setFont (RoadView::font (28.0f));
    g.drawText (ru ("Гараж"), 26, 12, 200, 32, juce::Justification::centredLeft);
    g.setColour (juce::Colour (ink).withAlpha (0.6f));
    g.setFont (RoadView::font (14.0f));
    g.drawText (ru ("Сверху вниз — порядок эффектов"), 26, 40, 220, 16, juce::Justification::centredLeft);

    const auto order = dragCar >= 0 ? dragOrder : processor.getCarOrder();
    for (int slot = 0; slot < RoadView::numCars; ++slot)
    {
        auto& y = shownY[static_cast<size_t> (order[static_cast<size_t> (slot)])];
        const float target = rowBounds (slot).getY();
        y = placed ? y + (target - y) * 0.3f : target;
    }
    placed = true;

    const auto drawRow = [&] (int slot, int car, float y, bool lifted)
    {
        const auto r = rowBounds (slot).withY (y);
        const bool hidden = road.isHidden (car), working = RoadView::isWorking (car);
        if (lifted)
        {
            g.setColour (juce::Colours::black.withAlpha (0.3f));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 6.0f);
        }
        g.setColour (juce::Colours::white.withAlpha (lifted ? 0.85f : slot == hoverSlot ? 0.45f : 0.3f));
        g.fillRoundedRectangle (r, 6.0f);
        if (road.getSelected() == car) // открытая на дороге
        {
            g.setColour (juce::Colour (accent).withAlpha (0.45f));
            g.fillRoundedRectangle (r, 6.0f);
        }
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        // Ручка перетаскивания: 2 × 3 точки.
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        for (int i = 0; i < 6; ++i)
            g.fillEllipse (r.getX() + 8.0f + 5.0f * static_cast<float> (i % 2), r.getCentreY() - 7.5f + 5.0f * static_cast<float> (i / 2), 3.0f, 3.0f);

        road.drawThumbnail (g, car, { r.getX() + 22.0f, r.getY() + 4.0f, 74.0f, r.getHeight() - 7.0f }, hidden ? 0.3f : 1.0f);

        const auto toggle = toggleBounds (r);
        const float textX = r.getX() + 104.0f, textW = toggle.getX() - 6.0f - textX;
        g.setColour (juce::Colour (ink).withAlpha (hidden ? 0.45f : 1.0f));
        g.setFont (RoadView::font (17.0f));
        g.drawText (RoadView::effectName (car), juce::Rectangle<float> (textX, r.getY() + 2.0f, textW, 18.0f), juce::Justification::centredLeft);
        g.setColour (juce::Colour (ink).withAlpha (hidden ? 0.35f : 0.6f));
        g.setFont (RoadView::font (13.5f));
        g.drawText (RoadView::carModel (car) + (working ? juce::String() : ru (" · скоро")),
                    juce::Rectangle<float> (textX, r.getY() + 19.0f, textW, 15.0f), juce::Justification::centredLeft);

        // Тумблер «на дороге»: зелёный, как «ВКЛ» у таблички.
        g.setColour (hidden ? juce::Colours::black.withAlpha (0.25f) : juce::Colour (0xff57c22e));
        g.fillRoundedRectangle (toggle, toggle.getHeight() * 0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (toggle, toggle.getHeight() * 0.5f, 1.0f);
        const float d = toggle.getHeight() - 4.0f;
        g.setColour (juce::Colours::white);
        g.fillEllipse (hidden ? toggle.getX() + 2.0f : toggle.getRight() - 2.0f - d, toggle.getY() + 2.0f, d, d);
    };

    int liftedSlot = -1;
    for (int slot = 0; slot < RoadView::numCars; ++slot)
    {
        const int car = order[static_cast<size_t> (slot)];
        if (dragging && car == dragCar)
            liftedSlot = slot;
        else
            drawRow (slot, car, shownY[static_cast<size_t> (car)], false);
    }
    if (liftedSlot >= 0) // перетаскиваемая — поверх остальных
        drawRow (liftedSlot, dragCar, dragY, true);
}

void GaragePanel::mouseMove (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.position.y);
    const bool onRow = slot >= 0 && rowBounds (slot).contains (e.position);
    const bool onToggle = onRow && toggleBounds (rowBounds (slot)).contains (e.position);
    hoverSlot = onRow ? slot : -1;
    setMouseCursor (onToggle ? juce::MouseCursor::PointingHandCursor
                    : onRow  ? juce::MouseCursor::DraggingHandCursor
                             : juce::MouseCursor::NormalCursor);
}

void GaragePanel::mouseExit (const juce::MouseEvent&)
{
    hoverSlot = -1;
}

void GaragePanel::mouseDown (const juce::MouseEvent& e)
{
    const int slot = slotAt (e.position.y);
    if (slot < 0 || ! rowBounds (slot).contains (e.position))
        return;
    const auto order = processor.getCarOrder();
    const int car = order[static_cast<size_t> (slot)];
    if (toggleBounds (rowBounds (slot)).contains (e.position))
    {
        road.setHidden (car, ! road.isHidden (car));
        repaint();
        return;
    }
    dragCar = car;
    dragOrder = order;
    dragY = rowBounds (slot).getY();
    grab = e.position.y - dragY;
    dragging = false;
}

void GaragePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragCar < 0 || (! dragging && e.getDistanceFromDragStart() < 4))
        return;
    dragging = true;
    dragY = juce::jlimit (rowBounds (0).getY(), rowBounds (RoadView::numCars - 1).getY(), e.position.y - grab);

    // Перетаскиваемая встаёт в ближайшее место, остальные сдвигаются.
    const auto from = std::find (dragOrder.begin(), dragOrder.end(), dragCar);
    const auto to = dragOrder.begin() + juce::roundToInt ((dragY - rowsTop) / rowStep);
    if (from < to)
        std::rotate (from, from + 1, to + 1);
    else if (to < from)
        std::rotate (to, from, from + 1);
    repaint();
}

void GaragePanel::mouseUp (const juce::MouseEvent&)
{
    if (dragCar < 0)
        return;
    if (dragging)
    {
        shownY[static_cast<size_t> (dragCar)] = dragY; // доезжает до места от точки, где отпустили
        if (dragOrder != processor.getCarOrder())
            processor.setCarOrder (dragOrder);
    }
    else if (RoadView::isWorking (dragCar) && ! road.isHidden (dragCar)) // просто клик — открыть на дороге
        road.select (road.getSelected() == dragCar ? -1 : dragCar);
    dragCar = -1;
    dragging = false;
    repaint();
}

// ---------------- редактор ----------------

GentraAudioProcessorEditor::GentraAudioProcessorEditor (GentraAudioProcessor& p)
    : AudioProcessorEditor (&p), gentraProcessor (p), road (p.apvts, [&p] { return p.takeBassLevel(); }, [&p] (double& beat, double& bpm) { return p.beatNow (beat, bpm); },
                                                           [&p] (int car) { return p.effectLevel (car); }),
      eqGraph (p.apvts), garage (p, road)
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

    // «По такту» (дилей): металлическая табличка, включённая подсвечена.
    syncButton.setButtonText (ru ("По такту"));
    syncButton.setClickingTogglesState (true);
    syncButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    syncButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    syncButton.setTitle (ru ("Дилей по темпу DAW"));
    popup.addChildComponent (syncButton);

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
    popup.addChildComponent (eqGraph);
    content.addChildComponent (popup);

    // Гараж: кнопка в углу выдвигает панель справа; закрытая панель ждёт за правым краем.
    garageButton.setButtonText (ru ("Гараж"));
    garageButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    garageButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    garageButton.onClick = [this] { showGarage (! garageOpen); };
    content.addAndMakeVisible (garageButton);

    garage.close.setImages (false, true, true, close, 1.0f, {}, close, 1.0f, shade, close, 1.0f, juce::Colours::black.withAlpha (0.3f));
    garage.close.onClick = [this] { showGarage (false); };
    content.addAndMakeVisible (garage);
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
    o.filenameSuffix = "ui.settings"; // «.settings» без «ui» занят Standalone-обёрткой JUCE (аудиоустройство) — затёрли бы
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

    placePopup (static_cast<int> (rows.size()));

    garageButton.setTransform ({});
    garageButton.setBounds (RoadView::width - 8 - 96, 8, 96, 34);
    rotate (garageButton, 1.2f);

    garage.setBounds (garageArea (garageOpen));
}

juce::Rectangle<int> GentraAudioProcessorEditor::garageArea (bool open)
{
    return { open ? RoadView::width - 8 - GaragePanel::width : RoadView::width + 4, 8, GaragePanel::width, RoadView::height - 16 };
}

// Табличка под число строк доп. параметров: у машины с одним параметром она ниже, без пустой строки.
void GentraAudioProcessorEditor::placePopup (int numRows)
{
    popupRows = numRows;
    popup.setTransform ({});
    // По центру открытой части дороги (гараж справа её сужает).
    const int free = garageOpen ? garageArea (true).getX() : RoadView::width;
    // Пропорции при двух строках — как у panel.png (760×213); винты в углах, поэтому поля по 34.
    // С графиком (белая) — шире: ползунки короче, график между ними и крестиком; влезает и при открытом гараже.
    const int h = 58 + numRows * 24, w = popupGraph ? 580 : 380;
    popup.setBounds ((free - w) / 2, RoadView::height - 8 - h, w, h);
    onButton.setBounds (34, 12, 151, 30);
    syncButton.setBounds (196, 13, 104, 28);
    closeButton.setBounds (w - 34 - 36, 12, 36, 30);
    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
    {
        auto& row = rows[static_cast<size_t> (i)];
        const int y = 48 + i * 24;
        row.label.setBounds (34, y, popupGraph ? 72 : 88, 22);
        row.slider.setBounds (popupGraph ? 106 : 122, y, popupGraph ? 170 : 224, 22);
    }
    eqGraph.setBounds (288, 12, w - 288 - 12 - 36 - 34, h - 24);
    eqGraph.setVisible (popupGraph);
    rotate (popup, -0.6f);
}

void GentraAudioProcessorEditor::showGarage (bool show, bool animate)
{
    garageOpen = show;
    road.setVisibleWidth (static_cast<float> (show ? garageArea (true).getX() : RoadView::width));
    if (animate)
        juce::Desktop::getInstance().getAnimator().animateComponent (&garage, garageArea (show), 1.0f, 260, false, 1.0, 0.0);
    else
        garage.setBounds (garageArea (show));
    placePopup (popupRows);
}

void GentraAudioProcessorEditor::showPopup (int car)
{
    onAttachment.reset();
    syncAttachment.reset();
    for (auto& row : rows)
        row.attachment.reset();

    popup.setVisible (car >= 0);
    if (car < 0)
        return;

    const auto& carExtras = extras[static_cast<size_t> (car)];
    popupGraph = car == gentra::car::white;
    placePopup (static_cast<int> (std::count_if (carExtras.begin(), carExtras.end(), [] (const Extra& e) { return e.id != nullptr; })));

    auto& apvts = gentraProcessor.apvts;
    const auto& info = RoadView::working[static_cast<size_t> (car)];
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, info.on, onButton);
    syncButton.setVisible (info.sync != nullptr);
    if (info.sync != nullptr)
        syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, info.sync, syncButton);

    for (size_t i = 0; i < rows.size(); ++i)
    {
        const auto& extra = extras[static_cast<size_t> (car)][i];
        auto& row = rows[i];
        row.label.setVisible (extra.id != nullptr);
        row.slider.setVisible (extra.id != nullptr);
        if (extra.id == nullptr)
            continue;
        row.label.setText (ru (extra.label), juce::dontSendNotification);
        row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, extra.id, row.slider);
        // Attachment ставит свой форматтер — подменяем на русские единицы.
        const auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (extra.id));
        row.slider.textFromValueFunction = [extra, choice] (double v)
        {
            if (choice != nullptr) // длительность ноты: «1/4», «1/16T»
                return choice->choices[juce::jlimit (0, choice->choices.size() - 1, juce::roundToInt (v))];
            if (std::abs (v) < 0.05)
                v = 0.0; // без «-0.0»
            return (extra.sign && v > 0.0 ? "+" : "") + juce::String (v, extra.decimals) + " " + ru (extra.unit);
        };
        row.slider.setTitle (ru (extra.label));
        row.slider.updateText();
    }
}
