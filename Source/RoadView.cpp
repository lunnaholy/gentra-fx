#include "RoadView.h"

#include "BinaryData.h"

namespace
{
// ======================== ГРАФИКА: КАК ЗАМЕНИТЬ ========================
// Все картинки — PNG в Assets/, вшиваются в плагин при сборке. Замените файл с тем же именем
// и пересоберите. Размеры ниже — в пикселях окна; разрешение картинки может быть любым,
// важны только пропорции.
//   sky.jpg            900×300   небо до горизонта (растягивается)
//   mountains.png      543×60    горы, бесшовно по горизонтали, низ = горизонт
//   skyline.png        694×140   город, бесшовно, низ = горизонт
//   road.jpg           1333×390  дорога от верха бордюра (y = 184.5) вниз, бесшовно; полосы — laneY()
//   lamp.png           40×150    фонарь, повторяется каждые 280, низ = верх дороги
//   props.png          740×90    обочина, бесшовно; земля на 88 от верха
//   car_<цвет>.png     кузов сбоку без колёс, нос вправо; ширина = длина машины, низ = асфальт
//   wheel.png          колесо анфас, квадрат, колесо вписано в края
//   tint.png, rust.png накладки того же размера, что car_black / car_rust: тонировка, ржавчина
//   plate.png          190×36    номер в углу
// Положение колёс на кузове — в долях длины машины:
constexpr float rearWheelX = 0.222f, frontWheelX = 0.81f;
constexpr float wheelY = 0.083f; // центр колеса над асфальтом
constexpr float wheelR = 0.083f; // радиус колеса
// Тряска от низа: смещение в пикселях окна при пике низа 0 dBFS и время затухания, с.
constexpr float shakePx = 8.0f, shakeRelease = 0.12f;
// Под бит DAW: сколько держится уровень низа между долями, с; во сколько слабее несильные доли.
constexpr float beatHold = 0.6f, weakBeat = 0.6f;
// =======================================================================

constexpr float W = static_cast<float> (RoadView::width);
constexpr float H = static_cast<float> (RoadView::height);
constexpr float carLen = 150.0f, laneGap = 60.0f, roadTop = 200.0f;
constexpr int lanes = 4, shoulder = 4;
// Шашки: скорость потока (px/с на единицу ve), время перестроения, горизонт прогноза,
// дальше какого места впереди не смотрим, цена одного перестроения (px места).
constexpr float flowSpeed = 190.0f, laneChangeTime = 0.6f, foresight = 1.0f;
constexpr float lookahead = carLen * 4.0f, laneCost = carLen * 0.25f;
// Куда можно встроиться: минимум места до машины спереди и сзади (между центрами).
constexpr float mergeFront = carLen * 1.5f, mergeBack = carLen * 1.3f;
// Длина «кольца», по которому едет поток, = ширина экрана: уехавшая за край машина
// тут же въезжает с другой стороны и целиком не пропадает никогда.
constexpr float span = W;
constexpr float pi = juce::MathConstants<float>::pi;

constexpr float laneY (float lane) { return roadTop + laneGap * (0.8f + lane); }
constexpr float laneScale (float lane) { return 0.8f + 0.06f * lane; }
constexpr float roadBottom = laneY (static_cast<float> (shoulder)) + laneGap * 0.3f;

struct CarInfo
{
    const char* asset;
    const char* name;
    juce::uint32 colour;
    bool on;        // для декоративных
    float strength; // для декоративных: сила «эффекта» = скорость
};

// Порядок и значения — как в прототипе.
const std::array<CarInfo, RoadView::numCars> carInfo { {
    { "car_black_png", "Саб и тонировка", 0xff1b1c21, true, 0.0f },
    { "car_rust_png", "Low-res", 0xffb7a688, true, 0.0f },
    { "car_red_png", "Дисторшн", 0xffb3202a, true, 0.2475f },
    { "car_green_png", "Питчер", 0xff1f8a3b, false, 0.0f },
    { "car_orange_png", "Форманта", 0xfff07a13, false, 0.0f },
    { "car_purple_png", "Гармонайзер", 0xff7a2bbf, true, 0.35f },
    { "car_blue_png", "Хорус", 0xff2456c8, true, 0.128f },
    { "car_pink_png", "Фэйзер", 0xffff5fa2, false, 0.0f },
    { "car_silver_png", "Дилей", 0xffa3acb6, true, 0.15f },
    { "car_grey_png", "Реверб", 0xff4d545c, true, 0.165f },
    { "car_yellow_png", "Гейт", 0xfff2c230, false, 0.0f },
    { "car_white_png", "Эквалайзер", 0xffeef0f2, true, 0.1667f },
} };
enum { black, rustCar, red, green, orange, purple, blue, pink, silver, grey, yellow, white };

juce::Image asset (const char* name) { return RoadView::image (name); }

const juce::Identifier hiddenId { "hiddenCars" }; // битовая маска скрытых машин в состоянии плагина

float posMod (float a, float n)
{
    const float m = std::fmod (a, n);
    return m < 0.0f ? m + n : m;
}

float ahead (float from, float to) { return posMod (to - from, span); }           // расстояние вперёд по кольцу
float offset (float a, float b) { return ahead (b, a + span * 0.5f) - span * 0.5f; } // a − b по кольцу, со знаком
float wrap (float d) { return posMod (d + span * 0.5f, span) - span * 0.5f; }          // разница по кольцу в −span/2..span/2

// Повторяет картинку с шагом period: копия на x = k·period − scroll, видимая часть — [x0, x1].
void drawStrip (juce::Graphics& g, const juce::Image& img, float scrollX, float period,
                float w, float h, float top, float x0, float x1)
{
    float x = -posMod (scrollX, period);
    x -= period * std::ceil ((x - x0) / period);
    const float overlap = w >= period ? 1.0f : 0.0f; // без щелей между бесшовными кусками
    for (; x < x1; x += period)
        g.drawImage (img, { x, top, w + overlap, h });
}

// Картинка машины (или накладка) в единицах машины: x 0..1, y вверх от асфальта.
void drawInCarUnits (juce::Graphics& g, const juce::Image& img, float alpha,
                     const juce::AffineTransform& extra = {})
{
    if (! img.isValid() || alpha <= 0.0f)
        return;
    const auto w = static_cast<float> (img.getWidth());
    const auto h = static_cast<float> (img.getHeight());
    g.setOpacity (alpha);
    g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / w, -1.0f / w).translated (0.0f, h / w).followedBy (extra));
}

juce::Colour rgba (int r, int gr, int b, float a)
{
    return juce::Colour (static_cast<juce::uint8> (r), static_cast<juce::uint8> (gr), static_cast<juce::uint8> (b), juce::jlimit (0.0f, 1.0f, a));
}

juce::String ru (const char* s) { return juce::String::fromUTF8 (s); }
}

const std::array<RoadView::Working, RoadView::numWorking> RoadView::working { {
    { "bass", "САБ", "bassMix", "bassOn" },
    { "lores", "LO-RES", "loresMix", "loresOn" },
} };

juce::Image RoadView::image (const char* name)
{
    int size = 0;
    const char* data = BinaryData::getNamedResource (name, size);
    jassert (data != nullptr);
    return data != nullptr ? juce::ImageFileFormat::loadFrom (data, static_cast<size_t> (size)) : juce::Image();
}

juce::Font RoadView::font (float size)
{
    // PT Sans Narrow Bold (ParaType Free Font License — можно вшивать), вшит из Assets/.
    static const auto typeface = juce::Typeface::createSystemTypefaceFor (BinaryData::PTSansNarrowBold_ttf,
                                                                         static_cast<size_t> (BinaryData::PTSansNarrowBold_ttfSize));
    return juce::FontOptions (typeface).withHeight (size);
}

juce::String RoadView::effectName (int car) { return ru (carInfo[static_cast<size_t> (car)].name); }
juce::Colour RoadView::bodyColour (int car) { return juce::Colour (carInfo[static_cast<size_t> (car)].colour); }

RoadView::RoadView (juce::AudioProcessorValueTreeState& state, std::function<float()> level, BeatClock clock)
    : apvts (state), bassLevel (std::move (level)), beatClock (std::move (clock)),
      sky (asset ("sky_jpg")), mountains (asset ("mountains_png")), skyline (asset ("skyline_png")),
      road (asset ("road_jpg")), lamp (asset ("lamp_png")), props (asset ("props_png")),
      wheel (asset ("wheel_png")), tint (asset ("tint_png")), rust (asset ("rust_png")), plate (asset ("plate_png")), tag (asset ("tag_png")),
      camX (span * 0.5f), focusY (H * 0.5f), centreY (H * 0.5f), lastFocusY (H * 0.5f)
{
    for (size_t i = 0; i < cars.size(); ++i)
    {
        bodies[i] = asset (carInfo[i].asset);

        auto& c = cars[i];
        c.lane = static_cast<int> (i) % lanes;
        c.lp = static_cast<float> (c.lane);
        c.x = (static_cast<float> (i) + 0.5f) * span / static_cast<float> (numCars);
    }

    hiddenMask = static_cast<int> (apvts.state.getProperty (hiddenId, 0));
    place();
    setOpaque (true);
    setSize (width, height);
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

// ---------------- состояние ----------------

float RoadView::param (const char* id) const
{
    return apvts.getRawParameterValue (id)->load();
}

bool RoadView::isOn (int car) const
{
    return car < numWorking ? param (working[static_cast<size_t> (car)].on) > 0.5f
                            : carInfo[static_cast<size_t> (car)].on;
}

float RoadView::strength (int car) const
{
    if (! isOn (car))
        return 0.0f;
    if (car == black)
        return juce::jlimit (0.0f, 1.0f, juce::jmax (param ("bass"), param ("tint")) * 0.01f * param ("bassMix") * 0.01f);
    if (car == rustCar)
        return param ("lores") * 0.01f * param ("loresMix") * 0.01f;
    return carInfo[static_cast<size_t> (car)].strength;
}

// Машина целиком (кузов и колёса) в рамке area: нос вправо, колёса на нижней кромке.
void RoadView::drawThumbnail (juce::Graphics& g, int car, juce::Rectangle<float> area, float alpha) const
{
    const auto& body = bodies[static_cast<size_t> (car)];
    const float aspect = static_cast<float> (body.getHeight()) / static_cast<float> (juce::jmax (1, body.getWidth()));
    const float len = juce::jmin (area.getWidth(), area.getHeight() / aspect);
    const auto toArea = juce::AffineTransform::scale (len, -len).translated (area.getCentreX() - len * 0.5f, area.getBottom());

    juce::Graphics::ScopedSaveState s (g);
    drawInCarUnits (g, body, alpha, toArea);
    const float ww = static_cast<float> (juce::jmax (1, wheel.getWidth())), wh = static_cast<float> (juce::jmax (1, wheel.getHeight()));
    g.setOpacity (alpha);
    for (float wx : { rearWheelX, frontWheelX })
        g.drawImageTransformed (wheel, juce::AffineTransform::translation (-ww * 0.5f, -wh * 0.5f)
                                           .scaled (2.0f * wheelR / ww, -2.0f * wheelR / wh)
                                           .translated (wx, wheelY)
                                           .followedBy (toArea));
}

bool RoadView::isHidden (int car) const
{
    return (static_cast<int> (apvts.state.getProperty (hiddenId, 0)) >> car) & 1;
}

void RoadView::setHidden (int car, bool hide)
{
    const int mask = static_cast<int> (apvts.state.getProperty (hiddenId, 0));
    hiddenMask = hide ? mask | (1 << car) : mask & ~(1 << car);
    apvts.state.setProperty (hiddenId, hiddenMask, nullptr);
    // Скрытый рабочий эффект выключается, вернувшийся в гараже — включается.
    if (car < numWorking)
        if (auto* on = apvts.getParameter (working[static_cast<size_t> (car)].on))
        {
            on->beginChangeGesture();
            on->setValueNotifyingHost (hide ? 0.0f : 1.0f);
            on->endChangeGesture();
        }
    if (hide && car == selected)
        select (-1);
    if (hide && car == hovered)
        hovered = -1;
    repaint();
}

void RoadView::select (int car)
{
    if (car == selected)
        return;
    selected = car;
    drag.wheel = -1;
    if (onSelectionChanged != nullptr)
        onSelectionChanged (car);
}

// ---------------- движение (как в прототипе) ----------------

// Можно ли встать в полосу прямо сейчас: спереди и сзади есть место, с запасом на разницу скоростей
// (быстрая машина сзади или медленная спереди съедят зазор за время перестроения).
bool RoadView::laneFree (int car, int lane) const
{
    const auto& c = cars[static_cast<size_t> (car)];
    for (size_t o = 0; o < cars.size(); ++o)
    {
        const auto& q = cars[o];
        if (static_cast<int> (o) == car || hidden (static_cast<int> (o)) || (q.lane != lane && std::abs (q.lp - static_cast<float> (lane)) > 0.5f))
            continue;
        const float closing = (q.ve - c.ve) * flowSpeed * laneChangeTime; // > 0 — сближаемся сзади
        const float d = ahead (c.x, q.x);
        if (d < mergeFront - juce::jmin (0.0f, closing) || d > span - mergeBack - juce::jmax (0.0f, closing))
            return false;
    }
    return true;
}

// Сколько места будет перед машиной в полосе через foresight секунд (пустая полоса — lookahead).
float RoadView::room (int car, int lane) const
{
    const auto& c = cars[static_cast<size_t> (car)];
    float best = lookahead;
    for (size_t o = 0; o < cars.size(); ++o)
    {
        const auto& q = cars[o];
        if (static_cast<int> (o) == car || hidden (static_cast<int> (o)) || (q.lane != lane && std::abs (q.lp - static_cast<float> (lane)) > 0.5f))
            continue;
        if (const float d = ahead (c.x, q.x); d < span * 0.6f) // дальше — уже машина сзади
            best = juce::jmin (best, d + (q.ve - c.ve) * flowSpeed * foresight);
    }
    return best;
}

// Шашки: поиск пути по полосам. Цель — полоса с наибольшим прогнозным местом впереди минус цена
// за каждое перестроение; путь к ней идёт через соседние полосы, и каждая на пути должна быть
// свободна сейчас. Машина делает первый шаг пути; следующий — после остывания, с новым планом.
void RoadView::weave (int car)
{
    auto& c = cars[static_cast<size_t> (car)];
    const float here = room (car, c.lane);
    if (here >= lookahead)
        return; // впереди пусто — незачем

    const int top = selected >= 0 ? lanes - 2 : lanes - 1; // ближняя полоса занята открытой машиной
    // Упёрся в медленного — уходит в любую свободную полосу, где просторнее; иначе нужен запас,
    // чтобы не дёргаться туда-сюда. Перестроение дальше соседней полосы стоит laneCost за каждую.
    const bool stuck = here < mergeFront;
    int step = 0;
    float best = here + (stuck ? 0.0f : carLen * 0.3f + laneCost);
    for (int dir : { -1, 1 })
        for (int lane = c.lane + dir; lane >= 0 && lane <= top && laneFree (car, lane); lane += dir)
            if (const float score = room (car, lane) - laneCost * static_cast<float> (std::abs (lane - c.lane) - 1); score > best)
            {
                best = score;
                step = dir;
            }

    if (step != 0)
    {
        c.lane += step;
        c.cool = laneChangeTime;
        ++laneChanges;
    }
}

float RoadView::closestGap() const
{
    float gap = span;
    for (size_t i = 0; i < cars.size(); ++i)
        for (size_t o = 0; o < cars.size(); ++o)
            if (i != o && ! hidden (static_cast<int> (i)) && ! hidden (static_cast<int> (o)) && std::abs (cars[i].lp - cars[o].lp) < 0.5f)
                gap = juce::jmin (gap, ahead (cars[i].x, cars[o].x));
    return gap;
}

void RoadView::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    advance (juce::jlimit (0.0f, 0.05f, static_cast<float> ((now - lastTick) * 0.001)));
    lastTick = now;
}

void RoadView::advance (float dt)
{
    time += dt;

    // Маска могла смениться из пресета.
    hiddenMask = static_cast<int> (apvts.state.getProperty (hiddenId, 0));
    if (selected >= 0 && hidden (selected))
        select (-1);

    for (int i = 0; i < numCars; ++i)
    {
        if (hidden (i))
            continue; // скрытая едет «призраком»: ни на кого не реагирует, никому не мешает
        auto& c = cars[static_cast<size_t> (i)];
        const bool on = isOn (i);
        const float s = strength (i);
        float target = on ? 0.2f + s : 0.06f;
        c.cool -= dt;

        int blocker = -1;
        float dist = 1.0e9f;
        for (int o = 0; o < numCars; ++o)
        {
            if (o == i || hidden (o) || cars[static_cast<size_t> (o)].lane != c.lane)
                continue;
            const float d = ahead (c.x, cars[static_cast<size_t> (o)].x);
            if (d < dist)
            {
                dist = d;
                blocker = o;
            }
        }
        const bool close = blocker >= 0 && dist < carLen * 1.5f;

        if (i == selected)
        {
            // Открытая машина пробирается в ближнюю полосу — по одной, только в свободное место.
            if (const int step = juce::jlimit (-1, 1, lanes - 1 - c.lane); step != 0 && c.cool <= 0.0f && laneFree (i, c.lane + step))
            {
                c.lane += step;
                c.cool = laneChangeTime;
            }
        }
        else if (! on)
        {
            if (c.lane != shoulder && laneFree (i, shoulder))
                c.lane = shoulder; // выключенная — на обочину
        }
        else if (c.lane == shoulder)
        {
            const int back = selected >= 0 ? lanes - 2 : lanes - 1;
            if (laneFree (i, back))
            {
                c.lane = back;
                c.cool = 1.0f;
            }
        }
        else if (selected >= 0 && c.lane == lanes - 1)
        {
            if (laneFree (i, lanes - 2)) // уступает ближнюю полосу открытой машине,
                c.lane = lanes - 2;
            else if (laneFree (i, shoulder)) // а если некуда — уходит на обочину
                c.lane = shoulder;
        }
        else if (c.cool <= 0.0f)
        {
            weave (i);
        }

        // Догнал — едет со скоростью переднего; ближе дистанции — отстаёт, пока зазор не восстановится.
        if (close)
            target = juce::jmin (target, cars[static_cast<size_t> (blocker)].ve * juce::jmin (1.0f, dist / (carLen * 1.2f)));
        c.ve += (target - c.ve) * juce::jmin (1.0f, dt * 3.0f);
    }

    float cam = 0.0f;
    for (const auto& c : cars)
        cam += c.ve / static_cast<float> (numCars);

    scroll += dt * (80.0f + 520.0f * cam);
    for (auto& c : cars)
    {
        c.x = posMod (c.x + (c.ve - cam) * flowSpeed * dt, span);
        c.lp += (static_cast<float> (c.lane) - c.lp) * juce::jmin (1.0f, dt * 5.0f);
        c.spin += dt * (3.0f + c.ve * 16.0f);
    }

    // Камера едет за открытой машиной и приближается к ней.
    const Car* f = selected >= 0 ? &cars[static_cast<size_t> (selected)] : nullptr;
    const float follow = juce::jmin (1.0f, dt * (f != nullptr ? 12.0f : 5.0f));
    const float step = offset (f != nullptr ? f->x : span * 0.5f, camX) * follow;
    camX = posMod (camX + step, span);
    camU += step;
    focus += ((f != nullptr ? 1.0f : 0.0f) - focus) * juce::jmin (1.0f, dt * 4.0f);

    if (f != nullptr)
    {
        lastFocusY = laneY (f->lp) - 0.12f * carLen;
        zoomGoal = juce::jmin (W * 0.72f, H * 1.4f) / (carLen * laneScale (f->lp));
    }
    zoom = 1.0f + (zoomGoal - 1.0f) * focus;
    focusY = juce::jmap (focus, H * 0.5f, lastFocusY);
    centreY = juce::jmap (focus, H * 0.5f, H * 0.47f);

    // Тряска: мгновенная атака, экспоненциальный спад. Земля прыгает сильнее, чем ездит вбок.
    const float level = bassLevel != nullptr ? juce::jlimit (0.0f, 1.0f, bassLevel()) : 0.0f;
    double beat = 0.0, bpm = 120.0;
    if (beatClock != nullptr && beatClock (beat, bpm))
    {
        // Транспорт играет: удар ровно на каждой доле (сильная — сильнее), сила — от низа за последние доли.
        bassHold = juce::jmax (level, bassHold * std::exp (-dt / beatHold));
        const double frac = beat - std::floor (beat);
        const auto sinceBeat = static_cast<float> (frac * 60.0 / bpm);
        rumble = bassHold * (beat < 1.0 ? 1.0f : weakBeat) * std::exp (-sinceBeat / shakeRelease);
        pulse = static_cast<float> (frac);
    }
    else
    {
        // Стоит: тряска прямо от пиков низа.
        bassHold = level;
        rumble = juce::jmax (level, rumble * std::exp (-dt / shakeRelease));
        pulse = std::fmod (time * 2.0f, 1.0f);
    }
    shake = { rumble * shakePx * 0.4f * (rng.nextFloat() * 2.0f - 1.0f), rumble * shakePx * (rng.nextFloat() * 2.0f - 1.0f) };

    place();
    repaint();
}

void RoadView::place()
{
    for (auto& c : cars)
    {
        c.px = W * 0.5f + offset (c.x, camX);
        c.py = laneY (c.lp);
        c.sc = laneScale (c.lp);
    }
}

// ---------------- геометрия экрана ----------------

juce::Point<float> RoadView::toScreen (float worldX, float worldY) const
{
    return { (worldX - W * 0.5f) * zoom + W * 0.5f + shake.x, (worldY - focusY) * zoom + centreY + shake.y };
}

juce::Point<float> RoadView::wheelCentre (int w) const
{
    const auto& c = cars[static_cast<size_t> (selected)];
    const float x = w == 0 ? rearWheelX : frontWheelX;
    return toScreen (c.px + (x - 0.5f) * carLen * c.sc, c.py - wheelY * carLen * c.sc);
}

float RoadView::wheelHitRadius() const
{
    return 0.07f * carLen * cars[static_cast<size_t> (selected)].sc * zoom * 1.7f;
}

bool RoadView::knobsVisible() const
{
    return selected >= 0 && focus > 0.6f;
}

int RoadView::carAt (juce::Point<float> p) const
{
    const float x = (p.x - shake.x - W * 0.5f) / zoom + W * 0.5f;
    const float y = (p.y - shake.y - centreY) / zoom + focusY;

    int best = -1;
    for (int i = 0; i < numCars; ++i) // ближние полосы перекрывают дальние
    {
        if (hidden (i))
            continue;
        const auto& c = cars[static_cast<size_t> (i)];
        const float half = carLen * c.sc * 0.6f;
        if (std::abs (wrap (x - c.px)) < half && y < c.py + 10.0f && y > c.py - 0.42f * carLen * c.sc
            && (best < 0 || c.lp > cars[static_cast<size_t> (best)].lp))
            best = i;
    }
    return best;
}

int RoadView::wheelAt (juce::Point<float> p) const
{
    if (! knobsVisible())
        return -1;
    for (int w = 0; w < 2; ++w)
        if (p.getDistanceFrom (wheelCentre (w)) < wheelHitRadius())
            return w;
    return -1;
}

juce::RangedAudioParameter* RoadView::knobParam (int w) const
{
    const auto& info = working[static_cast<size_t> (selected)];
    return apvts.getParameter (w == 0 ? info.main : info.mix);
}

// ---------------- мышь ----------------

void RoadView::mouseMove (const juce::MouseEvent& e)
{
    hovered = carAt (e.position);
    const bool clickable = wheelAt (e.position) >= 0 || (hovered >= 0 && hovered < numWorking);
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void RoadView::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
}

void RoadView::mouseDown (const juce::MouseEvent& e)
{
    if (const int w = wheelAt (e.position); w >= 0)
    {
        auto* p = knobParam (w);
        const auto c = wheelCentre (w);
        drag = { w, std::atan2 (e.position.y - c.y, e.position.x - c.x), p->convertFrom0to1 (p->getValue()) };
        p->beginChangeGesture();
        return;
    }

    const int car = carAt (e.position);
    if (car >= 0 && car < numWorking)
        select (car);
    else if (car < 0)
        select (-1);
}

void RoadView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag.wheel < 0 || ! knobsVisible())
        return;

    // Колесо крутится по кругу: 270° = весь диапазон.
    auto* p = knobParam (drag.wheel);
    const auto c = wheelCentre (drag.wheel);
    const float a = std::atan2 (e.position.y - c.y, e.position.x - c.x);
    float d = a - drag.angle;
    if (d > pi) d -= 2.0f * pi;
    if (d < -pi) d += 2.0f * pi;
    drag.angle = a;

    const auto& range = p->getNormalisableRange();
    drag.value = juce::jlimit (range.start, range.end, drag.value + d / (1.5f * pi) * (range.end - range.start));
    p->setValueNotifyingHost (p->convertTo0to1 (drag.value));
}

void RoadView::mouseUp (const juce::MouseEvent&)
{
    if (drag.wheel >= 0 && selected >= 0)
        knobParam (drag.wheel)->endChangeGesture();
    drag.wheel = -1;
}

void RoadView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const int w = wheelAt (e.position); w >= 0)
    {
        auto* p = knobParam (w);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
}

void RoadView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheelDetails)
{
    if (const int w = wheelAt (e.position); w >= 0)
    {
        auto* p = knobParam (w);
        const float delta = (wheelDetails.isReversed ? -1.0f : 1.0f) * wheelDetails.deltaY * 0.1f;
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + delta));
        p->endChangeGesture();
    }
}

// ---------------- отрисовка ----------------

void RoadView::paint (juce::Graphics& g)
{
    // Сразу в окно, без промежуточного буфера: при любом масштабе окна сцена остаётся чёткой.
    {
        juce::Graphics::ScopedSaveState s (g);
        drawScene (g);
    }
    drawOverlays (g);
}

void RoadView::drawScene (juce::Graphics& g) const
{
    const float horizon = (roadTop - focusY) * zoom + centreY;
    const float cityScale = juce::jlimit (1.0f, 1.9f, roadTop / 150.0f);
    const float ground = scroll + camU;

    g.drawImage (sky, { 0.0f, 0.0f, W, juce::jmax (40.0f, horizon) });
    drawStrip (g, mountains, ground * 0.03f, 543.0f, 543.0f, 60.0f * cityScale, horizon - 60.0f * cityScale, 0.0f, W);

    {
        // Город чуть приближается вместе с камерой.
        const float zs = (1.0f + (zoom - 1.0f) * 0.4f) * cityScale;
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::translation (-W * 0.5f, 0.0f).scaled (zs).translated (W * 0.5f, horizon));
        drawStrip (g, skyline, ground * 0.1f, 694.0f, 694.0f, 140.0f, -140.0f, W * 0.5f - W * 0.5f / zs, W * 0.5f + W * 0.5f / zs);
    }

    juce::Graphics::ScopedSaveState save (g);
    // Земля и машины трясутся, небо и город — нет.
    g.addTransform (juce::AffineTransform::translation (-W * 0.5f, -focusY).scaled (zoom).translated (W * 0.5f + shake.x, centreY + shake.y));
    const float margin = 2.0f + shakePx / zoom;
    const float x0 = W * 0.5f - W * 0.5f / zoom - margin, x1 = W * 0.5f + W * 0.5f / zoom + margin;

    drawStrip (g, road, ground, 1333.0f, 1333.0f, 390.0f, roadTop - 15.5f, x0, x1);
    drawStrip (g, props, ground * 1.1f, 740.0f, 740.0f, 90.0f, roadBottom + 40.0f - 88.0f, x0, x1);
    drawStrip (g, lamp, ground * 0.8f, 280.0f, 40.0f, 150.0f, roadTop - 150.0f, x0, x1);

    std::array<int, numCars> order {};
    for (int i = 0; i < numCars; ++i)
        order[static_cast<size_t> (i)] = i;
    std::sort (order.begin(), order.end(), [this] (int a, int b) { return cars[static_cast<size_t> (a)].lp < cars[static_cast<size_t> (b)].lp; });

    // Машина у края рисуется и копией с другой стороны кольца.
    const float reach = carLen * 1.7f; // с учётом шлейфов эффектов за машиной
    for (int i : order)
        if (! hidden (i))
            for (float k : { -1.0f, 0.0f, 1.0f })
                if (const float px = cars[static_cast<size_t> (i)].px + k * span; px + reach > x0 && px - reach < x1)
                    drawCar (g, i, px);
}

void RoadView::drawCar (juce::Graphics& g, int i, float px) const
{
    const auto& c = cars[static_cast<size_t> (i)];
    const auto& body = bodies[static_cast<size_t> (i)];
    const bool on = isOn (i);
    const float phase = pulse;

    juce::Graphics::ScopedSaveState save (g);
    // Дальше всё в единицах машины: x 0..1 от зада к носу, y вверх от асфальта.
    g.addTransform (juce::AffineTransform::translation (-0.5f, 0.0f).scaled (carLen * c.sc, -carLen * c.sc).translated (px, c.py));

    // Тень
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::scale (1.0f, 0.05f, 0.5f, 0.004f));
        juce::ColourGradient shadow (rgba (0, 0, 0, 0.55f), 0.5f, 0.004f, rgba (0, 0, 0, 0.0f), 1.06f, 0.004f, true);
        shadow.addColour (0.09, rgba (0, 0, 0, 0.55f));
        g.setGradientFill (shadow);
        g.fillEllipse (-0.06f, -0.556f, 1.12f, 1.12f);
    }

    // Эффекты позади кузова: свои у каждой машины
    const float sub = param ("bass") * 0.01f;
    const float blackMix = isOn (black) ? param ("bassMix") * 0.01f : 0.0f;

    if (i == red)
    {
        const float len = (0.1f + 0.45f * 0.55f * 0.45f) * (0.8f + 0.2f * std::sin (time * 40.0f));
        juce::ColourGradient flame (juce::Colour (0xfffff2a8), 0.03f, 0.0f, rgba (255, 60, 0, 0.0f), 0.03f - len, 0.0f, false);
        flame.addColour (0.4, juce::Colour (0xffff8a1f));
        g.setGradientFill (flame);
        juce::Path p;
        p.addTriangle (0.04f, 0.07f, 0.03f - len, 0.095f, 0.04f, 0.12f);
        g.fillPath (p);
    }
    else if (i == blue)
    {
        juce::Path p;
        for (int n = 0; n <= 20; ++n)
        {
            const float fn = static_cast<float> (n);
            const float y = 0.13f + std::sin (fn * 0.9f - time * 3.6f) * 0.09f * 0.4f * 0.5f * (fn / 20.0f + 0.2f);
            if (n == 0) p.startNewSubPath (0.0f, y);
            else p.lineTo (-fn * 0.04f, y);
        }
        g.setColour (rgba (120, 170, 255, 0.75f));
        g.strokePath (p, juce::PathStrokeType (0.012f));
    }
    else if (i == silver)
    {
        for (int n = 3; n >= 1; --n)
            drawInCarUnits (g, body, juce::jmin (0.5f, 0.4f * 0.3f * 0.75f / static_cast<float> (n)),
                            juce::AffineTransform::translation (-static_cast<float> (n) * 0.16f * 0.8f, 0.0f));
    }
    else if (i == purple)
    {
        for (int n = 2; n >= 1; --n)
        {
            const float fn = static_cast<float> (n);
            drawInCarUnits (g, body, 0.4f * 0.35f + 0.08f,
                            juce::AffineTransform::translation (-0.5f - fn * 0.07f, 0.2f * fn).scaled (1.0f - fn * 0.12f).translated (0.5f, 0.0f));
        }
    }
    else if (i == grey)
    {
        drawInCarUnits (g, body, 0.3f * 0.3f * 0.55f, juce::AffineTransform::scale (1.0f, -0.55f));
    }
    else if (i == black && blackMix > 0.0f && sub > 0.0f)
    {
        const float r = 0.07f + phase * (0.08f + 0.22f * sub);
        g.setColour (rgba (255, 182, 41, (1.0f - phase) * sub * blackMix));
        g.drawEllipse (0.09f - r, 0.16f - r, 2.0f * r, 2.0f * r, 0.012f);
    }

    // Свет фар на асфальте
    {
        juce::ColourGradient beam (rgba (255, 240, 190, i == white && on ? 0.3f : 0.16f), 0.95f, 0.0f,
                                   rgba (255, 240, 190, 0.0f), 1.6f, 0.0f, false);
        g.setGradientFill (beam);
        juce::Path p;
        p.startNewSubPath (0.95f, 0.2f);
        p.lineTo (1.6f, 0.22f);
        p.lineTo (1.6f, -0.02f);
        p.lineTo (0.95f, 0.16f);
        p.closeSubPath();
        g.fillPath (p);
    }

    // Кузов и колёса
    {
        juce::Graphics::ScopedSaveState s (g);
        const float alpha = on ? 1.0f : 0.7f;
        if (i == black && blackMix > 0.0f)
            g.addTransform (juce::AffineTransform::translation (0.0f, 0.009f * sub * blackMix * juce::jmax (0.0f, 1.0f - phase * 3.0f)));

        drawInCarUnits (g, body, alpha);
        if (i == black)
            drawInCarUnits (g, tint, alpha * param ("tint") * 0.01f);
        if (i == rustCar)
            drawInCarUnits (g, rust, alpha * (5.0f + 18.0f * (on ? strength (rustCar) : 0.2f)) / 23.0f);

        const bool knobs = i == selected && focus > 0.5f;
        const auto* mainParam = knobs ? knobParam (0) : nullptr;
        const auto* mixParam = knobs ? knobParam (1) : nullptr;
        const float knob[2] = { mainParam != nullptr ? mainParam->getValue() : 0.0f,
                                mixParam != nullptr ? mixParam->getValue() : 0.0f };

        const float ww = static_cast<float> (juce::jmax (1, wheel.getWidth()));
        const float wh = static_cast<float> (juce::jmax (1, wheel.getHeight()));
        for (int w = 0; w < 2; ++w)
        {
            const float wx = w == 0 ? rearWheelX : frontWheelX;
            // Колесо-ручка повёрнуто на своё значение, обычное — катится.
            const float angle = knobs ? knob[w] * 1.5f * pi : c.spin;
            g.setOpacity (alpha);
            g.drawImageTransformed (wheel, juce::AffineTransform::translation (-ww * 0.5f, -wh * 0.5f)
                                               .scaled (2.0f * wheelR / ww, -2.0f * wheelR / wh)
                                               .rotated (-angle)
                                               .translated (wx, wheelY));

            if (knobs)
            {
                const auto arc = [&] (float amount, juce::Colour colour)
                {
                    juce::Path p;
                    for (int n = 0; n <= 24; ++n)
                    {
                        const float a = (-135.0f + 270.0f * amount * static_cast<float> (n) / 24.0f) * pi / 180.0f;
                        const juce::Point<float> pt (wx + 0.1f * std::sin (a), wheelY + 0.1f * std::cos (a));
                        if (n == 0) p.startNewSubPath (pt);
                        else p.lineTo (pt);
                    }
                    g.setColour (colour);
                    g.strokePath (p, juce::PathStrokeType (0.016f));
                };
                arc (1.0f, rgba (232, 162, 58, 0.3f));
                if (knob[w] > 0.004f)
                    arc (knob[w], juce::Colour (0xffe8a23a));
            }
        }
    }

    // Ореолы фар и фонарей
    const auto glow = [&] (float x, float y, float r, juce::Colour colour)
    {
        g.setGradientFill (juce::ColourGradient (colour, x, y, colour.withAlpha (0.0f), x + r, y, true));
        g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
    };
    glow (0.934f, 0.185f, 0.09f, rgba (255, 240, 180, 0.55f));
    glow (0.05f, 0.227f, 0.06f, rgba (255, 40, 40, 0.5f));

    // Выключенная машина мигает аварийкой
    if (! on && phase < 0.5f)
    {
        g.setColour (juce::Colour (0xffff9d1c));
        g.fillEllipse (0.934f - 0.018f, 0.185f - 0.018f, 0.036f, 0.036f);
        g.fillEllipse (0.05f - 0.018f, 0.227f - 0.018f, 0.036f, 0.036f);
        g.fillEllipse (0.731f - 0.012f, 0.2f - 0.012f, 0.024f, 0.024f);
    }
}

void RoadView::drawOverlays (juce::Graphics& g) const
{
    // Номер в углу
    if (plate.isValid())
    {
        const float pw = 190.0f, ph = 36.0f;
        g.setOpacity (1.0f);
        g.drawImageTransformed (plate, juce::AffineTransform::scale (pw / static_cast<float> (plate.getWidth()), ph / static_cast<float> (plate.getHeight()))
                                           .translated (6.0f, 6.0f)
                                           .rotated (-2.0f * pi / 180.0f, 6.0f + pw * 0.5f, 6.0f + ph * 0.5f));
    }

    // Плашка над машиной: открытой или под мышью
    const int tagCar = selected >= 0 ? selected : hovered;
    if (tagCar >= 0)
    {
        const auto& c = cars[static_cast<size_t> (tagCar)];
        auto text = effectName (tagCar);
        if (tagCar >= numWorking)
            text << ru (" · скоро");

        const auto f = font (17.0f);
        const float th = 30.0f, tw = juce::GlyphArrangement::getStringWidth (f, text) + 40.0f;
        const auto top = toScreen (c.px, c.py - 0.36f * carLen * c.sc);
        const juce::Rectangle<float> box (juce::jlimit (70.0f, W - 70.0f, top.x) - tw * 0.5f, top.y - 6.0f - th, tw, th);

        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (-1.5f * pi / 180.0f, box.getCentreX(), box.getCentreY()));
        // Лента: рваные края как есть, середина тянется под длину текста.
        const int iw = tag.getWidth(), ih = tag.getHeight(), edge = iw / 8;
        const float e = static_cast<float> (edge) * th / static_cast<float> (ih);
        g.setOpacity (1.0f);
        g.drawImage (tag.getClippedImage ({ 0, 0, edge, ih }), box.withWidth (e));
        g.drawImage (tag.getClippedImage ({ edge, 0, iw - 2 * edge, ih }), box.reduced (e, 0.0f));
        g.drawImage (tag.getClippedImage ({ iw - edge, 0, edge, ih }), box.withLeft (box.getRight() - e));
        g.setColour (juce::Colour (0xff15161a));
        g.setFont (f);
        g.drawText (text, box, juce::Justification::centred);
    }

    // Подписи под колёсами-ручками
    if (knobsVisible())
    {
        const auto labelFont = font (15.0f);
        const auto valueFont = font (19.0f);

        for (int w = 0; w < 2; ++w)
        {
            const auto centre = wheelCentre (w);
            const float y = centre.y + wheelHitRadius();
            const auto* p = knobParam (w);
            const auto label = w == 0 ? ru (working[static_cast<size_t> (selected)].mainLabel) : juce::String ("MIX");
            const auto value = juce::String (juce::roundToInt (p->convertFrom0to1 (p->getValue()))) + " %";

            const auto line = [&] (const juce::String& text, const juce::Font& font, juce::Colour colour, float top)
            {
                const juce::Rectangle<float> r (centre.x - 80.0f, top, 160.0f, font.getHeight() + 2.0f);
                g.setFont (font);
                g.setColour (juce::Colours::black);
                g.drawText (text, r.translated (2.0f, 2.0f), juce::Justification::centred);
                g.setColour (colour);
                g.drawText (text, r, juce::Justification::centred);
            };
            line (label, labelFont, juce::Colours::white, y);
            line (value, valueFont, juce::Colour (0xffe8a23a), y + 16.0f);
        }
    }
}
