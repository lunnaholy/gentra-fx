#include "RoadView.h"

#include "BinaryData.h"
#include "Dsp.h"

namespace
{
// ======================== ГРАФИКА: КАК ЗАМЕНИТЬ ========================
// Все картинки — PNG в Assets/, вшиваются в плагин при сборке. Замените файл с тем же именем
// и пересоберите. Размеры ниже — в пикселях окна; разрешение картинки может быть любым,
// важны только пропорции.
//   sky.jpg            900×300   небо до горизонта (растягивается)
//   mountains.png      543×60    горы, бесшовно по горизонтали, низ = горизонт; на экране × mountainScale
//   skyline.png        694×140   город, бесшовно, низ = горизонт
//   road.jpg           1333×390  дорога от верха бордюра (y = 184.5) вниз, бесшовно; полосы — laneY()
//   lamp.png           40×150    фонарь, повторяется каждые 280, низ = верх дороги
//   props.png          740×90    обочина, бесшовно; земля на 88 от верха
//   car_<цвет>.png     кузов сбоку без колёс, нос вправо; ширина = длина машины, низ = асфальт;
//                      пропорции любые, колёса и огни — в Geo у машины (carInfo ниже);
//                      фото без колёс готовит и меряет Tools/skin.py
//   wheel.png          колесо анфас, квадрат, колесо вписано в края
//   tint.png, rust.png накладки тонировки и ржавчины, обрезаются по силуэту car_black / car_rust
//   plate.png          190×36    номер в углу
// Колёса и огни на кузове — в долях длины машины: x от зада к носу, y вверх от асфальта.
struct Spot { float x, y; };
struct Geo
{
    float rearX, frontX, wheelY, wheelR; // центры колёс по x, центр над асфальтом, радиус
    Spot head, tail, side, exhaust;      // фара, стоп, повторитель поворота, выхлоп
};
// Старый общий кузов 800×288.
constexpr Geo stock { 0.222f, 0.81f, 0.083f, 0.083f, { 0.934f, 0.185f }, { 0.05f, 0.227f }, { 0.731f, 0.2f }, { 0.035f, 0.095f } };
// Горы крупнее картинки, чтобы выглядывать из-за города (город ~187 px, низкая застройка ~60 px).
constexpr float mountainScale = 2.5f;
// Тряска от низа: смещение в пикселях окна при пике низа 0 dBFS и время затухания, с.
constexpr float shakePx = 8.0f, shakeRelease = 0.12f;
// Под бит DAW: сколько держится уровень низа между долями, с; во сколько слабее несильные доли.
constexpr float beatHold = 0.6f, weakBeat = 0.6f;
// Застрявший за медленным моргает дальним: через сколько секунд ожидания и сколько вспышек в секунду.
constexpr float flashDelay = 0.4f, flashRate = 14.0f;
// Моргает только быстрый (желаемая скорость от fastSpeed) и только тому, кто медленнее на flashMargin;
// тот уступает, а если некуда — разгоняется до скорости моргающего плюс yieldBoost.
constexpr float fastSpeed = 0.35f, flashMargin = 0.1f, yieldBoost = 0.05f;
// Уступающий втискивается плотнее обычного: доля от mergeFront/mergeBack.
constexpr float yieldSqueeze = 0.85f;
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
    const char* model; // подпись в гараже
    juce::uint32 colour;
    Geo geo = stock;
};

// Порядок и значения — как в прототипе.
const std::array<CarInfo, RoadView::numCars> carInfo { {
    { "car_black_png", "Саб и тонировка", "Gentra", 0xff1b1c21 },
    // Приора
    { "car_rust_png", "Low-res", "Приора", 0xffb7a688,
      { 0.2171f, 0.8121f, 0.0813f, 0.0814f, { 0.94f, 0.2f }, { 0.04f, 0.215f }, { 0.73f, 0.193f }, { 0.05f, 0.105f } } },
    // Жигули
    { "car_red_png", "Дисторшн", "Жигули", 0xffb3202a,
      { 0.2277f, 0.8369f, 0.0825f, 0.0823f, { 0.978f, 0.198f }, { 0.018f, 0.2f }, { 0.937f, 0.208f }, { 0.05f, 0.085f } } },
    // BYD Seagull: повторитель в зеркале
    { "car_green_png", "Тормоз ленты", "BYD Seagull", 0xff1f8a3b,
      { 0.1619f, 0.8181f, 0.0959f, 0.0957f, { 0.93f, 0.222f }, { 0.07f, 0.305f }, { 0.635f, 0.29f }, { 0.04f, 0.1f } } },
    // Дамас, белый (был оранжевым): повторитель у передней стойки
    { "car_orange_png", "Автопанорама", "Дамас", 0xffe8eaec,
      { 0.1956f, 0.7833f, 0.0923f, 0.0923f, { 0.98f, 0.23f }, { 0.022f, 0.26f }, { 0.558f, 0.355f }, { 0.04f, 0.11f } } },
    // Спарк, салатовый (был фиолетовым)
    { "car_purple_png", "Сайдчейн", "Спарк", 0xff93c02e,
      { 0.1566f, 0.8265f, 0.098f, 0.098f, { 0.923f, 0.249f }, { 0.059f, 0.298f }, { 0.734f, 0.22f }, { 0.04f, 0.12f } } },
    // Hongqi E-QM5, чёрный (был синим): повторитель в зеркале
    { "car_blue_png", "Хорус", "Hongqi E-QM5", 0xff15161a,
      { 0.2026f, 0.8118f, 0.0806f, 0.0805f, { 0.955f, 0.16f }, { 0.05f, 0.2f }, { 0.665f, 0.24f }, { 0.04f, 0.07f } } },
    // Лисян: повторитель в зеркале
    { "car_pink_png", "Компрессор", "Лисян", 0xffff5fa2,
      { 0.216f, 0.8189f, 0.0888f, 0.0889f, { 0.95f, 0.205f }, { 0.06f, 0.25f }, { 0.665f, 0.255f }, { 0.04f, 0.1f } } },
    // BYD Song Plus: повторитель в зеркале
    { "car_silver_png", "Дилей", "BYD Song Plus", 0xffa3acb6,
      { 0.2124f, 0.814f, 0.0883f, 0.0882f, { 0.94f, 0.2f }, { 0.06f, 0.25f }, { 0.67f, 0.27f }, { 0.04f, 0.1f } } },
    // BYD Yuan Up: повторитель в зеркале
    { "car_grey_png", "Реверб", "BYD Yuan Up", 0xff4d545c,
      { 0.1928f, 0.8344f, 0.0916f, 0.0914f, { 0.95f, 0.225f }, { 0.05f, 0.25f }, { 0.675f, 0.3f }, { 0.04f, 0.1f } } },
    // Zeekr 007: повторитель в зеркале
    { "car_yellow_png", "Гейт", "Zeekr 007", 0xfff2c230,
      { 0.2024f, 0.8086f, 0.0873f, 0.0872f, { 0.94f, 0.186f }, { 0.06f, 0.212f }, { 0.645f, 0.232f }, { 0.05f, 0.09f } } },
    // Нексия
    { "car_white_png", "Эквалайзер", "Нексия", 0xffeef0f2,
      { 0.2234f, 0.8154f, 0.0796f, 0.0796f, { 0.947f, 0.183f }, { 0.038f, 0.205f }, { 0.737f, 0.197f }, { 0.05f, 0.1f } } },
} };

const Geo& geo (int car) { return carInfo[static_cast<size_t> (car)].geo; }

// Накладка, подогнанная под кузов: растянута в его рамку и видна только там, где кузов непрозрачен.
juce::Image clipTo (const juce::Image& overlay, const juce::Image& body)
{
    if (! overlay.isValid() || ! body.isValid())
        return overlay;
    juce::Image out (juce::Image::ARGB, body.getWidth(), body.getHeight(), true);
    {
        juce::Graphics g (out);
        g.drawImage (overlay, out.getBounds().toFloat());
    }
    const auto argb = body.convertedToFormat (juce::Image::ARGB);
    const juce::Image::BitmapData src (argb, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dst (out, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < out.getHeight(); ++y)
        for (int x = 0; x < out.getWidth(); ++x)
            dst.setPixelColour (x, y, dst.getPixelColour (x, y).withMultipliedAlpha (src.getPixelColour (x, y).getFloatAlpha()));
    return out;
}
using namespace gentra::car;

juce::Image asset (const char* name) { return RoadView::image (name); }

const juce::Identifier hiddenId { "hiddenCars" };   // битовая маска скрытых машин в состоянии плагина
const juce::Identifier hiddenOffId { "hiddenOff" }; // какие из них были выключены до скрытия
// Все 12 в ширину экрана — тесно, быстрые моргают треть времени. По умолчанию три машины в гараже (UI-11),
// с ними моргание ~5–7% времени.
constexpr int defaultHidden = (1 << green) | (1 << orange) | (1 << purple);

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

const std::array<RoadView::Working, RoadView::numCars> RoadView::working = []
{
    std::array<Working, numCars> w {};
    w[black] = { "bass", "САБ", "%", 0, false, "bassMix", "bassOn" };
    w[rustCar] = { "lores", "LO-RES", "%", 0, false, "loresMix", "loresOn" };
    w[red] = { "drive", "ДРАЙВ", "%", 0, false, "distMix", "distOn" };
    w[white] = { "eqVolume", "ГРОМКОСТЬ", "дБ", 1, true, "eqMix", "eqOn" };
    w[silver] = { "delayTime", "ВРЕМЯ", "мс", 0, false, "delayMix", "delayOn", "delaySync", "delayNote" };
    w[blue] = { "chorusDepth", "ГЛУБИНА", "%", 0, false, "chorusMix", "chorusOn" };
    w[pink] = { "compAmount", "СЖАТИЕ", "%", 0, false, "compMix", "compOn" };
    w[grey] = { "reverbSize", "РАЗМЕР", "%", 0, false, "reverbMix", "reverbOn" };
    w[purple] = { "sidechainDepth", "ГЛУБИНА", "%", 0, false, "sidechainMix", "sidechainOn" };
    w[yellow] = { "gateDepth", "ГЛУБИНА", "%", 0, false, "gateMix", "gateOn" };
    w[green] = { "tapeAmount", "ТОРМОЗ", "%", 0, false, "tapeMix", "tapeOn" };
    w[orange] = { "panDepth", "ГЛУБИНА", "%", 0, false, "panMix", "panOn" };
    return w;
}();

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
juce::String RoadView::carModel (int car) { return ru (carInfo[static_cast<size_t> (car)].model); }
juce::Colour RoadView::bodyColour (int car) { return juce::Colour (carInfo[static_cast<size_t> (car)].colour); }

RoadView::RoadView (juce::AudioProcessorValueTreeState& state, std::function<float()> level, BeatClock clock, CarLevel effect)
    : apvts (state), bassLevel (std::move (level)), beatClock (std::move (clock)), carLevel (std::move (effect)),
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
    tint = clipTo (tint, bodies[black]);
    rust = clipTo (rust, bodies[rustCar]);

    hiddenMask = static_cast<int> (apvts.state.getProperty (hiddenId, defaultHidden));
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
    return isWorking (car) && param (working[static_cast<size_t> (car)].on) > 0.5f;
}

float RoadView::strength (int car) const
{
    if (! isOn (car))
        return 0.0f;
    if (car == black)
        return juce::jlimit (0.0f, 1.0f, juce::jmax (param ("bass"), param ("tint")) * 0.01f * param ("bassMix") * 0.01f);
    if (car == rustCar)
        return param ("lores") * 0.01f * param ("loresMix") * 0.01f;
    if (car == red)
        return param ("drive") * 0.01f * param ("distMix") * 0.01f;
    if (car == white) // насколько далеко от нуля самая выкрученная ручка
        return std::max ({ std::abs (param ("eqVolume")), std::abs (param ("eqLow")), std::abs (param ("eqMid")), std::abs (param ("eqHigh")) }) / 12.0f
               * param ("eqMix") * 0.01f;
    if (car == silver)
        return param ("delayMix") * 0.01f * (0.5f + 0.5f * param ("delayFeedback") / 90.0f);
    if (car == blue)
        return param ("chorusDepth") * 0.01f * param ("chorusMix") * 0.01f;
    if (car == pink)
        return param ("compAmount") * 0.01f * param ("compMix") * 0.01f;
    if (car == grey)
        return param ("reverbSize") * 0.01f * param ("reverbMix") * 0.01f;
    if (car == purple)
        return param ("sidechainDepth") * 0.01f * param ("sidechainMix") * 0.01f;
    if (car == yellow)
        return param ("gateDepth") * 0.01f * param ("gateMix") * 0.01f;
    if (car == green)
        return param ("tapeAmount") * 0.01f * param ("tapeMix") * 0.01f;
    if (car == orange)
        return juce::jmax (param ("panDepth"), std::abs (param ("panWidth"))) * 0.01f * param ("panMix") * 0.01f;
    return 0.0f;
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
    const auto& k = geo (car);
    g.setOpacity (alpha);
    for (float wx : { k.rearX, k.frontX })
        g.drawImageTransformed (wheel, juce::AffineTransform::translation (-ww * 0.5f, -wh * 0.5f)
                                           .scaled (2.0f * k.wheelR / ww, -2.0f * k.wheelR / wh)
                                           .translated (wx, k.wheelY)
                                           .followedBy (toArea));
}

bool RoadView::isHidden (int car) const
{
    return (static_cast<int> (apvts.state.getProperty (hiddenId, defaultHidden)) >> car) & 1;
}

void RoadView::setHidden (int car, bool hide)
{
    const int mask = static_cast<int> (apvts.state.getProperty (hiddenId, defaultHidden));
    hiddenMask = hide ? mask | (1 << car) : mask & ~(1 << car);
    apvts.state.setProperty (hiddenId, hiddenMask, nullptr);
    // Скрытый рабочий эффект выключается, вернувшийся из гаража — включается, если был включён до скрытия.
    if (isWorking (car))
        if (auto* on = apvts.getParameter (working[static_cast<size_t> (car)].on))
        {
            const int offMask = static_cast<int> (apvts.state.getProperty (hiddenOffId, 0));
            if (hide && ! ((mask >> car) & 1)) // повторное скрытие не затирает запомненное
                apvts.state.setProperty (hiddenOffId, isOn (car) ? offMask & ~(1 << car) : offMask | (1 << car), nullptr);
            on->beginChangeGesture();
            on->setValueNotifyingHost (! hide && ! ((offMask >> car) & 1) ? 1.0f : 0.0f);
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
bool RoadView::laneFree (int car, int lane, float squeeze) const
{
    const auto& c = cars[static_cast<size_t> (car)];
    for (size_t o = 0; o < cars.size(); ++o)
    {
        const auto& q = cars[o];
        if (static_cast<int> (o) == car || hidden (static_cast<int> (o)) || (q.lane != lane && std::abs (q.lp - static_cast<float> (lane)) > 0.5f))
            continue;
        const float closing = (q.ve - c.ve) * flowSpeed * laneChangeTime; // > 0 — сближаемся сзади
        const float d = ahead (c.x, q.x);
        if (d < mergeFront * squeeze - juce::jmin (0.0f, closing) || d > span - mergeBack * squeeze - juce::jmax (0.0f, closing))
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

int RoadView::flashingCars() const
{
    int n = 0;
    for (int i = 0; i < numCars; ++i)
        n += ! hidden (i) && cars[static_cast<size_t> (i)].waiting > flashDelay;
    return n;
}

// Уступить моргающему: соседняя свободная полоса, где просторнее, или обочина; false — некуда.
bool RoadView::yieldLane (int car)
{
    auto& c = cars[static_cast<size_t> (car)];
    const int top = selected >= 0 ? lanes - 2 : lanes - 1;
    int best = -1;
    for (int lane : { c.lane - 1, c.lane + 1 })
        if (lane >= 0 && lane <= top && laneFree (car, lane, yieldSqueeze) && (best < 0 || room (car, lane) > room (car, best)))
            best = lane;
    if (best < 0 && c.lane == lanes - 1 && laneFree (car, shoulder, yieldSqueeze))
        best = shoulder; // из ближней можно прижаться к обочине — потом вернётся сама
    if (best < 0)
        return false;
    c.lane = best;
    c.cool = laneChangeTime;
    ++laneChanges;
    ++yields;
    return true;
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
    hiddenMask = static_cast<int> (apvts.state.getProperty (hiddenId, defaultHidden));
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
        if (i == green)
            target *= 1.0f - 0.8f * level (green); // лента тормозит — и машина
        c.cool -= dt;
        const float pushSpeed = c.pushSpeed; // сзади моргают дальним: желаемая скорость моргающего (0 — нет)
        const bool pushed = pushSpeed > 0.0f;
        c.pushSpeed = 0.0f;

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

        // Быстрый упёрся в заметно более медленного и не может объехать — копит «ожидание»,
        // потом моргает дальним (см. drawCar), и передний обязан уступить.
        const bool waiting = on && i != selected && c.lane != shoulder && close && target >= fastSpeed
                             && blocker != selected // открытая занята своим перестроением
                             && cars[static_cast<size_t> (blocker)].ve < target - flashMargin;
        c.waiting = waiting ? c.waiting + dt : 0.0f;
        if (c.waiting > flashDelay)
        {
            auto& q = cars[static_cast<size_t> (blocker)];
            q.pushSpeed = juce::jmax (q.pushSpeed, target);
        }

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
            // Выключенная — на обочину, по одной полосе, иначе проехала бы сквозь машины в соседних.
            if (c.lane != shoulder && c.cool <= 0.0f && laneFree (i, c.lane + 1))
            {
                ++c.lane;
                c.cool = laneChangeTime;
            }
            else if (pushed && (c.cool > 0.0f || ! yieldLane (i)))
                target = juce::jmax (target, pushSpeed + yieldBoost); // не пускают на обочину, а сзади моргают — уступает как все
        }
        else if (c.lane == shoulder)
        {
            const int back = selected >= 0 ? lanes - 2 : lanes - 1;
            if (laneFree (i, back) && laneFree (i, lanes - 1)) // через ближнюю полосу — она тоже должна быть свободна
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
        else if (pushed)
        {
            // Моргают сзади: уйти в любую свободную соседнюю полосу, а если некуда — газануть и искать просвет.
            if (c.cool > 0.0f || ! yieldLane (i))
                target = juce::jmax (target, pushSpeed + yieldBoost);
        }
        else if (c.cool <= 0.0f)
        {
            weave (i);
        }

        // Уступающий сам упёрся в кого-то — передаёт «уступи» дальше по цепочке, до медленного лидера.
        if (pushed && close && blocker != selected)
        {
            auto& q = cars[static_cast<size_t> (blocker)];
            q.pushSpeed = juce::jmax (q.pushSpeed, pushSpeed);
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

    // Камера едет за открытой машиной и приближается к ней — в середине незакрытой гаражом части.
    viewSmooth += (viewWidth - viewSmooth) * juce::jmin (1.0f, dt * 6.0f);
    const Car* f = selected >= 0 ? &cars[static_cast<size_t> (selected)] : nullptr;
    const float follow = juce::jmin (1.0f, dt * (f != nullptr ? 12.0f : 5.0f));
    const float step = offset (f != nullptr ? f->x : span * 0.5f, camX) * follow;
    camX = posMod (camX + step, span);
    camU += step;
    focus += ((f != nullptr ? 1.0f : 0.0f) - focus) * juce::jmin (1.0f, dt * 4.0f);

    if (f != nullptr)
    {
        lastFocusY = laneY (f->lp) - 0.12f * carLen;
        zoomGoal = juce::jmin (viewSmooth * 0.72f, H * 1.4f) / (carLen * laneScale (f->lp));
    }
    zoom = 1.0f + (zoomGoal - 1.0f) * focus;
    focusY = juce::jmap (focus, H * 0.5f, lastFocusY);
    centreY = juce::jmap (focus, H * 0.5f, H * 0.47f);
    centreX = juce::jmap (focus, W * 0.5f, viewSmooth * 0.5f);

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

    // Номер на болте: пружина к покою, покой медленно гуляет, бас подталкивает. Под мышью — приподнимается.
    {
        const float rest = -2.0f + 1.2f * std::sin (time * 0.9f);
        plateSpin += (-30.0f * (plateAngle - rest) - 4.0f * plateSpin + 260.0f * rumble) * dt;
        plateAngle = juce::jlimit (-12.0f, 8.0f, plateAngle + plateSpin * dt);
        plateLift += ((plateHover ? 1.0f : 0.0f) - plateLift) * juce::jmin (1.0f, dt * 12.0f);
    }

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
    return { (worldX - W * 0.5f) * zoom + centreX + shake.x, (worldY - focusY) * zoom + centreY + shake.y };
}

juce::Point<float> RoadView::wheelCentre (int w) const
{
    const auto& c = cars[static_cast<size_t> (selected)];
    const auto& k = geo (selected);
    const float x = w == 0 ? k.rearX : k.frontX;
    return toScreen (c.px + (x - 0.5f) * carLen * c.sc, c.py - k.wheelY * carLen * c.sc);
}

float RoadView::wheelHitRadius() const
{
    return 0.84f * geo (selected).wheelR * carLen * cars[static_cast<size_t> (selected)].sc * zoom * 1.7f;
}

float RoadView::bodyHeight (int car) const
{
    const auto& body = bodies[static_cast<size_t> (car)];
    return static_cast<float> (body.getHeight()) / static_cast<float> (juce::jmax (1, body.getWidth()));
}

bool RoadView::knobsVisible() const
{
    return selected >= 0 && focus > 0.6f;
}

int RoadView::carAt (juce::Point<float> p) const
{
    const float x = (p.x - shake.x - centreX) / zoom + W * 0.5f;
    const float y = (p.y - shake.y - centreY) / zoom + focusY;

    int best = -1;
    for (int i = 0; i < numCars; ++i) // ближние полосы перекрывают дальние
    {
        if (hidden (i))
            continue;
        const auto& c = cars[static_cast<size_t> (i)];
        const float half = carLen * c.sc * 0.6f;
        if (std::abs (wrap (x - c.px)) < half && y < c.py + 10.0f && y > c.py - (bodyHeight (i) + 0.06f) * carLen * c.sc
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
    // По такту заднее колесо крутит длительность ноты, иначе — время в мс.
    const bool note = info.syncMain != nullptr && param (info.sync) > 0.5f;
    return apvts.getParameter (w == 0 ? (note ? info.syncMain : info.main) : info.mix);
}

// ---------------- мышь ----------------

void RoadView::mouseMove (const juce::MouseEvent& e)
{
    plateHover = onPlate (e.position);
    hovered = plateHover ? -1 : carAt (e.position);
    const bool clickable = plateHover || wheelAt (e.position) >= 0 || isWorking (hovered);
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void RoadView::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    plateHover = false;
}

void RoadView::mouseDown (const juce::MouseEvent& e)
{
    if (onPlate (e.position))
    {
        plateSpin += 120.0f; // щелчок — номер качнулся
        if (onLogoClicked)
            onLogoClicked();
        return;
    }

    if (const int w = wheelAt (e.position); w >= 0)
    {
        auto* p = knobParam (w);
        const auto c = wheelCentre (w);
        drag = { w, std::atan2 (e.position.y - c.y, e.position.x - c.x), p->convertFrom0to1 (p->getValue()) };
        p->beginChangeGesture();
        return;
    }

    const int car = carAt (e.position);
    if (isWorking (car))
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
    const float mw = 543.0f * mountainScale, mh = 60.0f * mountainScale;
    drawStrip (g, mountains, ground * 0.03f, mw, mw, mh, horizon - mh, 0.0f, W);

    {
        // Город чуть приближается вместе с камерой.
        const float zs = (1.0f + (zoom - 1.0f) * 0.4f) * cityScale;
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::translation (-W * 0.5f, 0.0f).scaled (zs).translated (W * 0.5f, horizon));
        drawStrip (g, skyline, ground * 0.1f, 694.0f, 694.0f, 140.0f, -140.0f, W * 0.5f - W * 0.5f / zs, W * 0.5f + W * 0.5f / zs);
    }

    juce::Graphics::ScopedSaveState save (g);
    // Земля и машины трясутся, небо и город — нет.
    g.addTransform (juce::AffineTransform::translation (-W * 0.5f, -focusY).scaled (zoom).translated (centreX + shake.x, centreY + shake.y));
    const float margin = 2.0f + shakePx / zoom;
    const float x0 = W * 0.5f - centreX / zoom - margin, x1 = W * 0.5f + (W - centreX) / zoom + margin;

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
    const auto& k = geo (i);

    juce::Graphics::ScopedSaveState save (g);
    // Дальше всё в единицах машины: x 0..1 от зада к носу, y вверх от асфальта.
    // Автопанорама: Дамас качается поперёк полосы — вправо (от нас, вверх по экрану) и влево (к нам).
    const float sway = i == orange ? 0.04f * carLen * c.sc * level (orange) : 0.0f;
    g.addTransform (juce::AffineTransform::translation (-0.5f, 0.0f).scaled (carLen * c.sc, -carLen * c.sc).translated (px, c.py - sway));

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

    if (i == red && on && param ("drive") > 0.0f)
    {
        // Пламя из выхлопа: длина от драйва, яркость пламени не зависит от Mix.
        const float len = (0.05f + 0.4f * param ("drive") * 0.01f) * (0.8f + 0.2f * std::sin (time * 40.0f));
        const auto e = k.exhaust;
        juce::ColourGradient flame (juce::Colour (0xfffff2a8), e.x - 0.005f, 0.0f, rgba (255, 60, 0, 0.0f), e.x - 0.005f - len, 0.0f, false);
        flame.addColour (0.4, juce::Colour (0xffff8a1f));
        g.setGradientFill (flame);
        juce::Path p;
        p.addTriangle (e.x + 0.005f, e.y - 0.025f, e.x - 0.005f - len, e.y, e.x + 0.005f, e.y + 0.025f);
        g.fillPath (p);
    }
    else if (i == blue && strength (blue) > 0.0f)
    {
        // Волна за машиной: размах от глубины и Mix, бег — со скоростью хоруса.
        const float amp = 0.05f * strength (blue), run = time * 2.0f * pi * param ("chorusRate");
        juce::Path p;
        for (int n = 0; n <= 20; ++n)
        {
            const float fn = static_cast<float> (n);
            const float y = 0.13f + std::sin (fn * 0.9f - run) * amp * (fn / 20.0f + 0.2f);
            if (n == 0) p.startNewSubPath (0.0f, y);
            else p.lineTo (-fn * 0.04f, y);
        }
        g.setColour (rgba (120, 170, 255, 0.75f));
        g.strokePath (p, juce::PathStrokeType (0.012f));
    }
    else if (i == silver && on)
    {
        // Повторы: копии сзади — шаг от времени, число и затухание от повторов, прозрачность от Mix.
        const float length = param ("delaySync") > 0.5f ? param ("delayNote") / static_cast<float> (gentra::numDelayDivisions - 1)
                                                         : (param ("delayTime") - 60.0f) / 540.0f;
        const float spacing = 0.1f + 0.35f * length;
        const float feedback = param ("delayFeedback") * 0.01f;
        const float mix = param ("delayMix") * 0.01f;
        const int copies = 1 + juce::roundToInt (feedback * 4.0f);
        for (int n = copies; n >= 1; --n)
            drawInCarUnits (g, body, 0.55f * mix * std::pow (juce::jmax (feedback, 0.3f), static_cast<float> (n - 1)),
                            juce::AffineTransform::translation (-static_cast<float> (n) * spacing, 0.0f));
    }
    else if (i == grey && strength (grey) > 0.0f)
    {
        // Отражение на мокром асфальте: длиннее с размером, заметнее с размером и Mix.
        const float size = param ("reverbSize") * 0.01f;
        drawInCarUnits (g, body, 0.08f + 0.35f * strength (grey), juce::AffineTransform::scale (1.0f, -(0.3f + 0.4f * size)));
    }
    else if (i == black && blackMix > 0.0f && sub > 0.0f)
    {
        const float r = 0.07f + phase * (0.08f + 0.22f * sub);
        g.setColour (rgba (255, 182, 41, (1.0f - phase) * sub * blackMix));
        g.drawEllipse (0.09f - r, 0.16f - r, 2.0f * r, 2.0f * r, 0.012f);
    }

    // Свет фар на асфальте
    {
        // Белая включена — фары ярче, и чем сильнее эквалайзер, тем ярче.
        const float beamAlpha = i == white && on ? 0.3f + 0.25f * strength (white) : 0.16f;
        const float x = k.head.x + 0.016f, y = k.head.y;
        juce::ColourGradient beam (rgba (255, 240, 190, beamAlpha), x, 0.0f,
                                   rgba (255, 240, 190, 0.0f), x + 0.65f, 0.0f, false);
        g.setGradientFill (beam);
        juce::Path p;
        p.startNewSubPath (x, y + 0.015f);
        p.lineTo (x + 0.65f, y + 0.035f);
        p.lineTo (x + 0.65f, -0.02f);
        p.lineTo (x, y - 0.025f);
        p.closeSubPath();
        g.fillPath (p);
    }

    // Кузов и колёса
    {
        juce::Graphics::ScopedSaveState s (g);
        const float alpha = on ? 1.0f : 0.7f;
        if (i == black && blackMix > 0.0f)
            g.addTransform (juce::AffineTransform::translation (0.0f, 0.009f * sub * blackMix * juce::jmax (0.0f, 1.0f - phase * 3.0f)));

        // Сайдчейн: кузов приседает на колёсах вместе с провалом громкости; компрессор: кузов сплющивается к асфальту,
        // чем сильнее подавление.
        drawInCarUnits (g, body, alpha, juce::AffineTransform::translation (0.0f, i == purple ? -0.03f * level (purple) : 0.0f)
                                            .scaled (1.0f, i == pink ? 1.0f - 0.12f * level (pink) : 1.0f));
        if (i == yellow && strength (yellow) > 0.0f)
        {
            // Шашечки такси на крыше: горят, пока гейт открыт, гаснут, когда закрыт.
            const float lit = 1.0f - level (yellow), top = bodyHeight (yellow) - 0.026f, x0 = 0.43f, cell = 0.018f;
            g.setColour (rgba (255, 220, 80, 0.5f * lit));
            g.fillRoundedRectangle (x0 - 0.02f, top - 0.012f, 6.0f * cell + 0.04f, 2.0f * cell + 0.03f, 0.02f);
            for (int row = 0; row < 2; ++row)
                for (int col = 0; col < 6; ++col)
                {
                    const bool dark = (row + col) % 2 == 0;
                    g.setColour (dark ? rgba (20, 20, 20, alpha) : rgba (255, 214, 40, alpha).interpolatedWith (juce::Colours::white, 0.6f * lit));
                    g.fillRect (x0 + cell * static_cast<float> (col), top + cell * static_cast<float> (row), cell, cell);
                }
        }
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
            const float wx = w == 0 ? k.rearX : k.frontX, wy = k.wheelY, arcR = 1.2f * k.wheelR;
            // Колесо-ручка повёрнуто на своё значение, обычное — катится.
            const float angle = knobs ? knob[w] * 1.5f * pi : c.spin;
            g.setOpacity (alpha);
            g.drawImageTransformed (wheel, juce::AffineTransform::translation (-ww * 0.5f, -wh * 0.5f)
                                               .scaled (2.0f * k.wheelR / ww, -2.0f * k.wheelR / wh)
                                               .rotated (-angle)
                                               .translated (wx, wy));

            if (knobs)
            {
                const auto arc = [&] (float from, float to, juce::Colour colour)
                {
                    juce::Path p;
                    for (int n = 0; n <= 24; ++n)
                    {
                        const float a = (-135.0f + 270.0f * (from + (to - from) * static_cast<float> (n) / 24.0f)) * pi / 180.0f;
                        const juce::Point<float> pt (wx + arcR * std::sin (a), wy + arcR * std::cos (a));
                        if (n == 0) p.startNewSubPath (pt);
                        else p.lineTo (pt);
                    }
                    g.setColour (colour);
                    g.strokePath (p, juce::PathStrokeType (0.016f));
                };
                arc (0.0f, 1.0f, rgba (232, 162, 58, 0.3f));
                // Ручка от минуса до плюса светится от середины (ноль), обычная — от начала.
                const float zero = w == 0 && working[static_cast<size_t> (i)].bipolar ? 0.5f : 0.0f;
                if (std::abs (knob[w] - zero) > 0.004f)
                    arc (zero, knob[w], juce::Colour (0xffe8a23a));
            }
        }
    }

    // Ореолы фар и фонарей
    const auto glow = [&] (float x, float y, float r, juce::Colour colour)
    {
        g.setGradientFill (juce::ColourGradient (colour, x, y, colour.withAlpha (0.0f), x + r, y, true));
        g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
    };
    glow (k.head.x, k.head.y, i == white && on ? 0.12f + 0.06f * strength (white) : 0.09f, rgba (255, 240, 180, 0.55f));
    // Лента тормозит — горят стоп-сигналы.
    const float braking = i == green ? level (green) : 0.0f;
    glow (k.tail.x, k.tail.y, 0.06f + 0.1f * braking, rgba (255, 40, 40, 0.5f + 0.5f * braking));

    // Ждёт, пока уступят, — стробит дальним: чистый белый, огромный ореол и луч.
    if (c.waiting > flashDelay && std::fmod (time * flashRate, 1.0f) < 0.5f)
    {
        const float x = k.head.x + 0.016f, y = k.head.y;
        juce::ColourGradient beam (juce::Colours::white, x, 0.0f, juce::Colours::white.withAlpha (0.0f), x + 1.25f, 0.0f, false);
        g.setGradientFill (beam);
        juce::Path p;
        p.startNewSubPath (x, y + 0.025f);
        p.lineTo (x + 1.25f, y + 0.215f);
        p.lineTo (x + 1.25f, y - 0.285f);
        p.lineTo (x, y - 0.035f);
        p.closeSubPath();
        g.fillPath (p);
        glow (k.head.x, k.head.y, 0.32f, juce::Colours::white);
        glow (k.head.x, k.head.y, 0.1f, juce::Colours::white);
    }

    // Выключенная машина мигает аварийкой
    if (! on && phase < 0.5f)
    {
        g.setColour (juce::Colour (0xffff9d1c));
        for (auto [s, r] : { std::pair { k.head, 0.018f }, { k.tail, 0.018f }, { k.side, 0.012f } })
            g.fillEllipse (s.x - r, s.y - r, 2.0f * r, 2.0f * r);
    }

    // Звук ушёл в сторону — мигает поворотник: левый (видимый борт) ярко, правый — отсветом с дальнего борта.
    if (const float pan = i == orange && on ? level (orange) : 0.0f; std::abs (pan) > 0.35f && std::fmod (time * 3.0f, 1.0f) < 0.5f)
    {
        const auto amber = juce::Colour (0xffff9d1c).withAlpha (pan < 0.0f ? 1.0f : 0.45f);
        for (auto [s, r] : { std::pair { k.head, 0.018f }, { k.tail, 0.018f }, { k.side, 0.012f } })
        {
            glow (s.x, s.y, 3.0f * r, amber.withMultipliedAlpha (0.6f));
            g.setColour (amber);
            g.fillEllipse (s.x - r, s.y - r, 2.0f * r, 2.0f * r);
        }
    }
}

namespace
{
constexpr float plateW = 190.0f, plateH = 36.0f;
constexpr juce::Point<float> platePivot { 14.0f, 18.0f }; // левый болт, в координатах номера
}

juce::AffineTransform RoadView::plateTransform() const
{
    const float lift = 1.0f + 0.05f * plateLift;
    return juce::AffineTransform::scale (lift, lift, platePivot.x, platePivot.y)
        .rotated (plateAngle * pi / 180.0f, platePivot.x, platePivot.y)
        .translated (6.0f, 6.0f - 2.0f * plateLift);
}

bool RoadView::onPlate (juce::Point<float> p) const
{
    return juce::Rectangle<float> (plateW, plateH).contains (p.transformedBy (plateTransform().inverted()));
}

void RoadView::drawOverlays (juce::Graphics& g) const
{
    // Номер в углу: качается, по нему пробегает блик (под мышью — чаще); клик — «О плагине».
    if (plate.isValid())
    {
        const auto t = plateTransform();
        if (plateLift > 0.01f) // приподнятый отбрасывает тень
        {
            juce::Path shadow;
            shadow.addRoundedRectangle (0.0f, 0.0f, plateW, plateH, 5.0f);
            g.setColour (juce::Colours::black.withAlpha (0.35f * plateLift));
            g.fillPath (shadow, t.translated (2.0f, 5.0f * plateLift));
        }
        g.setOpacity (1.0f);
        g.drawImageTransformed (plate, juce::AffineTransform::scale (plateW / static_cast<float> (plate.getWidth()),
                                                                     plateH / static_cast<float> (plate.getHeight()))
                                           .followedBy (t));

        const float period = plateHover ? 1.4f : 5.0f, sweep = std::fmod (time, period) / 0.7f;
        if (sweep < 1.0f)
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path shape;
            shape.addRoundedRectangle (0.0f, 0.0f, plateW, plateH, 5.0f);
            g.reduceClipRegion (shape, t);
            g.addTransform (t);
            const float x = -60.0f + (plateW + 120.0f) * sweep;
            juce::ColourGradient glint (juce::Colours::white.withAlpha (0.0f), x - 30.0f, 0.0f, juce::Colours::white.withAlpha (0.0f), x + 30.0f, 0.0f, false);
            glint.addColour (0.5, juce::Colours::white.withAlpha (0.55f));
            g.setGradientFill (glint);
            juce::Path band; // косая полоса
            band.addQuadrilateral (x - 30.0f, plateH, x + 6.0f, 0.0f, x + 36.0f, 0.0f, x, plateH);
            g.fillPath (band);
        }
    }

    // Плашка над машиной: открытой или под мышью
    const int tagCar = selected >= 0 ? selected : hovered;
    if (tagCar >= 0)
    {
        const auto& c = cars[static_cast<size_t> (tagCar)];
        const auto text = effectName (tagCar);

        const auto f = font (17.0f);
        const float th = 30.0f, tw = juce::GlyphArrangement::getStringWidth (f, text) + 40.0f;
        const auto top = toScreen (c.px, c.py - bodyHeight (tagCar) * carLen * c.sc);
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
            const auto& info = working[static_cast<size_t> (selected)];
            const bool note = w == 0 && info.sync != nullptr && param (info.sync) > 0.5f;
            const auto label = note ? ru ("НОТА") : w == 0 ? ru (info.mainLabel) : juce::String ("MIX");
            float v = p->convertFrom0to1 (p->getValue());
            if (std::abs (v) < 0.05f)
                v = 0.0f; // без «-0.0»
            juce::String value;
            if (note)
                value = p->getCurrentValueAsText(); // «1/8.», «1/4T»
            else if (w == 0)
                value = (info.bipolar && v > 0.0f ? "+" : "") + juce::String (v, info.mainDecimals) + " " + ru (info.mainUnit);
            else
                value = juce::String (juce::roundToInt (v)) + " %";

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
