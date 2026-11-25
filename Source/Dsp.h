#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

// DSP без зависимости от JUCE. Все amount — в диапазоне 0..1.
namespace gentra
{
constexpr int maxChannels = 2;

// ---- Константы для подстройки на слух ----
constexpr double bassMaxDb     = 12.0;    // усиление шельфа при 100%
constexpr double bassQ         = 0.707;   // без резонансного горба
constexpr float  clipThreshold = 0.8f;    // ниже порога клиппер ничего не делает (~ -2 dBFS)
constexpr float  fadeIn        = 0.05f;   // клиппер и тонировка плавно включаются на первых 5% хода
constexpr double tintOpenHz    = 20000.0; // срез тонировки при 0%
constexpr double tintClosedHz  = 800.0;   // срез тонировки при 100%
constexpr double loresMaxBits  = 16.0;    // битность при 0%
constexpr double driveMaxDb    = 36.0;    // предусиление дисторшна при 100%
constexpr double driveRefLevel = 0.25;    // синус этой амплитуды выходит с тем же пиком при любом драйве
constexpr double toneDarkHz    = 800.0;   // срез тона при 0%
constexpr double toneBrightHz  = 16000.0; // срез тона при 100% (дисторшн и дилей)
constexpr double delayMaxSeconds = 4.0;   // длина линии дилея: 1/2 при 30 BPM
constexpr double delayGlideTau = 0.05;    // постоянная плавной смены времени дилея, с (заметная езда ~0.2 с)
constexpr double reverbPreDelayMaxSeconds = 0.05; // пре-дилей реверба при размере 100%
constexpr float  reverbWetLevel = 0.33f;  // громкость хвоста (wetLevel juce::Reverb, его значение по умолчанию)

// Длительности нот для дилея по темпу, в четвертях (параметр «Нота» — индекс в этом списке).
// По возрастанию: каждая прямая нота, триоль следующей (вдвое длиннее) и она же с точкой.
constexpr int numDelayDivisions = 19;
constexpr std::array<double, numDelayDivisions> delayDivisionBeats {
    0.03125, 1.0 / 24.0, 0.046875, 0.0625, 1.0 / 12.0, 0.09375,
    0.125, 1.0 / 6.0, 0.1875, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0
};
constexpr std::array<const char*, numDelayDivisions> delayDivisionNames {
    "1/128", "1/64T", "1/128.", "1/64", "1/32T", "1/64.",
    "1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2"
};
constexpr int defaultDelayDivision = 12; // 1/8

// Длительность ноты в секундах при темпе bpm (четвертей в минуту); index — значение параметра «Нота».
inline double delayDivisionSeconds (float index, double bpm) noexcept
{
    const int i = std::clamp (static_cast<int> (std::lround (index)), 0, numDelayDivisions - 1);
    return delayDivisionBeats[static_cast<size_t> (i)] * 60.0 / bpm;
}

constexpr double pi = 3.14159265358979323846;

constexpr double sidechainAttackSeconds = 0.003; // провал сайдчейна в начале шага — не мгновенный, без щелчка
constexpr int defaultSidechainDivision = 15;    // 1/4

// Сайдчейн: сколько громкости убрать (0..1) на фазе шага 0..1. Возврат (1 − phase)^k: k от 0.25 (Форма 0 — провал
// держится почти весь шаг) до 4 (Форма 1 — резкий возврат); на стыке шагов короткая атака, громкость непрерывна.
inline double sidechainDuck (double phase, double stepSeconds, double shape) noexcept
{
    const double k = std::pow (2.0, 4.0 * shape - 2.0);
    return std::min (1.0, phase * stepSeconds / sidechainAttackSeconds) * std::pow (1.0 - phase, k);
}

// Остановка ленты: как часто — длительность цикла в четвертях (параметр «Каждые» — индекс в этом списке).
constexpr int numTapeCycles = 4;
constexpr std::array<double, numTapeCycles> tapeCycleBeats { 2.0, 4.0, 8.0, 16.0 };
constexpr std::array<const char*, numTapeCycles> tapeCycleNames { "1/2", "1 такт", "2 такта", "4 такта" };
constexpr int defaultTapeCycle = 1;
constexpr double tapeMaxSeconds = 10.0;      // линия: насколько лента успевает отстать (тормоз в полцикла 4 тактов при 50 BPM)
constexpr double tapeMinSpinSeconds = 0.005; // разгон не короче — без щелчка после остановки
constexpr double tapeCatchUpSeconds = 0.02;  // после разгона лента отстаёт — переход на живой вход за это время

// Скорость ленты на фазе цикла 0..1: в конце цикла (последние brake) тормозит 1 → 0, в начале (первые spin) разгоняется 0 → 1.
inline double tapeSpeed (double phase, double brake, double spin) noexcept
{
    if (brake > 0.0 && phase >= 1.0 - brake)
        return std::max (0.0, (1.0 - phase) / brake);
    if (spin > 0.0 && phase < spin)
        return phase / spin;
    return 1.0;
}

// Автопанорама: форма движения (параметр «Форма» — индекс в этом списке).
constexpr int numPanShapes = 5;
constexpr std::array<const char*, numPanShapes> panShapeNames { "Синус", "Треугольник", "Пила", "Пинг-понг", "Случайно" };
constexpr int defaultPanNote = 18;             // 1/2: туда и обратно за половинную
constexpr double panSmoothSeconds = 0.004;     // сглаживание положения: пила, пинг-понг и случайная прыгают без щелчков

// Положение −1 (лево) … +1 (право) на фазе цикла 0..1; random — значение случайной формы для этого цикла.
inline double panLfo (double phase, int shape, double random) noexcept
{
    switch (shape)
    {
        case 0: return std::sin (2.0 * pi * phase);
        case 1: return phase < 0.25 ? 4.0 * phase : phase < 0.75 ? 2.0 - 4.0 * phase : 4.0 * phase - 4.0;
        case 2: return 2.0 * phase - 1.0;
        case 3: return phase < 0.5 ? -1.0 : 1.0;
        default: return random;
    }
}

constexpr double gateMinRampSeconds = 0.001; // фронт гейта при Плавности 0 — без щелчка
constexpr int defaultGateDivision = 9;       // 1/16

// Гейт: насколько открыт (0..1) на фазе шага 0..1. Открыт первую половину шага; фронты — рампы шириной
// от 1 мс (Плавность 0) до четверти шага (Плавность 1, почти треугольник).
inline double gateOpen (double phase, double stepSeconds, double smooth) noexcept
{
    const double ramp = std::min (0.25, std::max (gateMinRampSeconds / stepSeconds, 0.25 * smooth));
    if (phase < 0.5)
        return std::min (1.0, phase / ramp);
    return std::max (0.0, 1.0 - (phase - 0.5) / ramp);
}

// Номера машин: на дороге, в маске гаража и в порядке цепочки.
namespace car
{
enum Id : int { black, rustCar, red, green, orange, purple, blue, pink, silver, grey, yellow, white, count };
}

inline bool changed (float a, float b) noexcept { return std::abs (a - b) > 0.0f; }

// Линейно до порога, выше — tanh-колено; выход всегда в [-1, 1].
inline float softClip (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= clipThreshold)
        return x;

    const float knee = 1.0f - clipThreshold;
    return std::copysign (clipThreshold + knee * std::tanh ((a - clipThreshold) / knee), x);
}

// Biquad (TDF-II) с коэффициентами RBJ. Смена коэффициентов не сбрасывает состояние.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    std::array<double, maxChannels> z1 {}, z2 {};

    void reset() noexcept
    {
        z1 = {};
        z2 = {};
    }

    double process (int channel, double in) noexcept
    {
        const auto ch = static_cast<size_t> (channel);
        const double out = b0 * in + z1[ch];
        z1[ch] = b1 * in - a1 * out + z2[ch];
        z2[ch] = b2 * in - a2 * out;
        return out;
    }

    // Усиление на частоте freq, дБ — для графика.
    double gainDb (double fs, double freq) const noexcept
    {
        const auto e1 = std::polar (1.0, -2.0 * pi * freq / fs), e2 = e1 * e1; // z^-1, z^-2 на единичной окружности
        return 20.0 * std::log10 (std::abs ((b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2)));
    }

    void setLowShelf (double fs, double freq, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * freq / fs;
        const double cosw = std::cos (w0);
        const double k = 2.0 * std::sqrt (A) * std::sin (w0) / (2.0 * q);
        const double a0 = (A + 1.0) + (A - 1.0) * cosw + k;

        b0 = A * ((A + 1.0) - (A - 1.0) * cosw + k) / a0;
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw) / a0;
        b2 = A * ((A + 1.0) - (A - 1.0) * cosw - k) / a0;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw) / a0;
        a2 = ((A + 1.0) + (A - 1.0) * cosw - k) / a0;
    }

    void setHighShelf (double fs, double freq, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * freq / fs;
        const double cosw = std::cos (w0);
        const double k = 2.0 * std::sqrt (A) * std::sin (w0) / (2.0 * q);
        const double a0 = (A + 1.0) - (A - 1.0) * cosw + k;

        b0 = A * ((A + 1.0) + (A - 1.0) * cosw + k) / a0;
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw) / a0;
        b2 = A * ((A + 1.0) + (A - 1.0) * cosw - k) / a0;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw) / a0;
        a2 = ((A + 1.0) - (A - 1.0) * cosw - k) / a0;
    }

    void setPeak (double fs, double freq, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * freq / fs;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;

        b0 = (1.0 + alpha * A) / a0;
        b1 = -2.0 * cosw / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = b1;
        a2 = (1.0 - alpha / A) / a0;
    }

    void setLowPass (double fs, double freq, double q) noexcept
    {
        const double w0 = 2.0 * pi * std::min (freq, 0.45 * fs) / fs;
        const double cosw = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        b0 = (1.0 - cosw) * 0.5 / a0;
        b1 = (1.0 - cosw) / a0;
        b2 = b0;
        a1 = -2.0 * cosw / a0;
        a2 = (1.0 - alpha) / a0;
    }
};

// Чёрная машина: low-shelf -> мягкий клиппер -> тонировка (lowpass).
class BassBoost
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        shelf.reset();
        tint.reset();
        last = { -1.0f, -1.0f, -1.0f };
        countdown = 0;
    }

    // Раз в сэмпл-фрейм; коэффициенты пересчитываются не чаще раза в 16 сэмплов.
    void setParams (float amount, float freqHz, float tintAmount) noexcept
    {
        clipMix = std::min (1.0f, amount / fadeIn);
        tintMix = std::min (1.0f, tintAmount / fadeIn);

        if (countdown > 0)
            --countdown;

        if (countdown == 0 && (changed (amount, last[0]) || changed (freqHz, last[1]) || changed (tintAmount, last[2])))
        {
            last = { amount, freqHz, tintAmount };
            shelf.setLowShelf (fs, freqHz, bassQ, bassMaxDb * static_cast<double> (amount));
            tint.setLowPass (fs, tintOpenHz * std::pow (tintClosedHz / tintOpenHz, static_cast<double> (tintAmount)), 0.707);
            countdown = 16;
        }
    }

    float processSample (int channel, float x) noexcept
    {
        auto y = static_cast<float> (shelf.process (channel, x));
        y += clipMix * (softClip (y) - y);

        // Фильтр тонировки работает всегда, чтобы при включении не было скачка из старого состояния.
        const auto dark = static_cast<float> (tint.process (channel, y));
        return y + tintMix * (dark - y);
    }

private:
    double fs = 44100.0;
    Biquad shelf, tint;
    std::array<float, 3> last {};
    float clipMix = 0.0f, tintMix = 0.0f;
    int countdown = 0;
};

// Ржавая машина: sample-and-hold с дробным шагом + квантование (mid-tread: тишина остаётся тишиной).
class LoRes
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        held = {};
        phase = 1.0;
        last = { -1.0f, -1.0f, -1.0f };
    }

    // minBits и minRateHz — битность и частота, до которых доходит эффект на 100%.
    void setParams (float amount, float minBits, float minRateHz) noexcept
    {
        if (changed (amount, last[0]) || changed (minBits, last[1]) || changed (minRateHz, last[2]))
        {
            last = { amount, minBits, minRateHz };
            const auto t = static_cast<double> (amount);
            // Частота: лог-интерполяция от fs (0%) до minRateHz (100%).
            increment = std::min (1.0, std::pow (std::min (1.0, static_cast<double> (minRateHz) / fs), t));
            const double bits = loresMaxBits + (static_cast<double> (minBits) - loresMaxBits) * t;
            levels = std::pow (2.0, bits - 1.0);
        }
    }

    // Раз в сэмпл-фрейм, общий для всех каналов: true — пора захватить новый сэмпл.
    bool tick() noexcept
    {
        phase += increment;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            return true;
        }
        return false;
    }

    float processSample (int channel, float x, bool capture) noexcept
    {
        const auto ch = static_cast<size_t> (channel);
        if (capture)
            held[ch] = static_cast<float> (std::round (static_cast<double> (x) * levels) / levels);
        return held[ch];
    }

private:
    double fs = 44100.0;
    double phase = 1.0;
    double increment = 1.0;
    double levels = 32768.0;
    std::array<float, maxChannels> held {};
    std::array<float, 3> last {};
};

// Красная машина: предусиление -> tanh -> тон (lowpass) -> компенсация громкости.
// Работает на удвоенной частоте (оверсэмплинг — в процессоре), fs — уже удвоенная.
class Drive
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        tone.reset();
        last = { -1.0f, -1.0f };
        countdown = 0;
    }

    void setParams (float amount, float toneAmount) noexcept
    {
        if (changed (amount, last[0]))
        {
            last[0] = amount;
            gain = static_cast<float> (std::pow (10.0, driveMaxDb * static_cast<double> (amount) / 20.0));
            comp = static_cast<float> (driveRefLevel / std::tanh (static_cast<double> (gain) * driveRefLevel));
        }

        if (countdown > 0)
            --countdown;

        if (countdown == 0 && changed (toneAmount, last[1]))
        {
            last[1] = toneAmount;
            tone.setLowPass (fs, toneDarkHz * std::pow (toneBrightHz / toneDarkHz, static_cast<double> (toneAmount)), 0.707);
            countdown = 16;
        }
    }

    float processSample (int channel, float x) noexcept
    {
        return comp * static_cast<float> (tone.process (channel, std::tanh (gain * x)));
    }

private:
    double fs = 88200.0;
    Biquad tone;
    float gain = 1.0f, comp = 1.0f;
    std::array<float, 2> last {};
    int countdown = 0;
};

inline double toneHz (float toneAmount) noexcept
{
    return toneDarkHz * std::pow (toneBrightHz / toneDarkHz, static_cast<double> (toneAmount));
}

// Белая машина: шельф «Низ», колокол «Середина», шельф «Верх», затем громкость.
struct EqSettings
{
    float volumeDb = 0.0f, lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f; // усиления, дБ
    float lowHz = 200.0f, midHz = 1000.0f, highHz = 4000.0f, midQ = 0.7f;

    bool bandsDiffer (const EqSettings& o) const noexcept
    {
        return changed (lowDb, o.lowDb) || changed (midDb, o.midDb) || changed (highDb, o.highDb) || changed (lowHz, o.lowHz)
               || changed (midHz, o.midHz) || changed (highHz, o.highHz) || changed (midQ, o.midQ);
    }
};

class Equalizer
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        low.reset();
        mid.reset();
        high.reset();
        last.lowHz = -1.0f; // заведомо «изменилось»
        gainDb = 1.0e9f;
        countdown = 0;
    }

    // Раз в сэмпл-фрейм; полосы пересчитываются не чаще раза в 16 сэмплов, громкость — сразу.
    void setParams (const EqSettings& s) noexcept
    {
        if (changed (s.volumeDb, gainDb))
        {
            gainDb = s.volumeDb;
            gain = static_cast<float> (std::pow (10.0, static_cast<double> (s.volumeDb) / 20.0));
        }

        if (countdown > 0)
            --countdown;

        if (countdown == 0 && s.bandsDiffer (last))
        {
            last = s;
            low.setLowShelf (fs, s.lowHz, 0.707, s.lowDb);
            mid.setPeak (fs, s.midHz, s.midQ, s.midDb);
            high.setHighShelf (fs, s.highHz, 0.707, s.highDb);
            countdown = 16;
        }
    }

    float processSample (int channel, float x) noexcept
    {
        return gain * static_cast<float> (high.process (channel, mid.process (channel, low.process (channel, x))));
    }

    // Итоговое усиление на частоте freq, дБ — для графика (по последним setParams).
    double gainDbAt (double freq) const noexcept
    {
        return static_cast<double> (gainDb) + low.gainDb (fs, freq) + mid.gainDb (fs, freq) + high.gainDb (fs, freq);
    }

private:
    double fs = 44100.0;
    Biquad low, mid, high;
    float gain = 1.0f, gainDb = 1.0e9f;
    EqSettings last;
    int countdown = 0;
};

// Серебряная машина: линия задержки с обратной связью, в петле — lowpass «тон».
// Смена времени — не скачком, а ездой головки чтения (как у ленты): два сглаживания подряд, поэтому
// плавны и положение, и скорость головки — ни щелчка, ни излома на старте и финише.
// Пока машина выключена, линия всё равно пишет вход (feed): включился — сразу слышны повторы недавнего.
class Delay
{
public:
    // Единственное место, где выделяется память, — до запуска звука.
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size = static_cast<int> (std::ceil (delayMaxSeconds * fs)) + 8;
        glide = 1.0 - std::exp (-1.0 / (delayGlideTau * fs));
        for (auto& l : line)
            l.assign (static_cast<size_t> (size), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : line)
            std::fill (l.begin(), l.end(), 0.0f);
        write = 0;
        delay = pull = -1.0;
        tone.reset();
        lastTone = -1.0f;
        countdown = 0;
    }

    // Раз в сэмпл-фрейм: время в секундах, повторы 0..1, тон 0..1.
    void setParams (double seconds, float feedbackAmount, float toneAmount) noexcept
    {
        // Цель — целое число сэмплов: доехав, головка читает без интерполяции и не мылит повторы.
        const double target = std::clamp (std::round (seconds * fs), 4.0, static_cast<double> (size - 4));
        if (delay < 0.0)
            delay = pull = target;
        pull += (target - pull) * glide;
        delay += (pull - delay) * glide;
        if (std::abs (target - pull) < 1.0e-3 && std::abs (target - delay) < 1.0e-3)
            delay = pull = target; // доехали: скорость уже ~0, дальше — целое число сэмплов

        feedback = feedbackAmount;
        if (countdown > 0)
            --countdown;
        if (countdown == 0 && changed (toneAmount, lastTone))
        {
            lastTone = toneAmount;
            tone.setLowPass (fs, toneHz (toneAmount), 0.707);
            countdown = 16;
        }
    }

    // Возвращает повтор и пишет в линию вход плюс отфильтрованную обратную связь.
    float processSample (int channel, float x) noexcept
    {
        const float y = read (channel);
        line[static_cast<size_t> (channel)][static_cast<size_t> (write)] = x + feedback * static_cast<float> (tone.process (channel, y));
        return y;
    }

    void feed (int channel, float x) noexcept { line[static_cast<size_t> (channel)][static_cast<size_t> (write)] = x; }

    // После всех каналов фрейма.
    void advance() noexcept { write = write + 1 < size ? write + 1 : 0; }

private:
    float at (int channel, int index) const noexcept
    {
        return line[static_cast<size_t> (channel)][static_cast<size_t> (index < 0 ? index + size : index)];
    }

    float read (int channel) const noexcept
    {
        const double whole = std::floor (delay);
        const auto frac = static_cast<float> (delay - whole);
        const int i = write - static_cast<int> (whole);
        if (frac <= 0.0f)
            return at (channel, i);

        // Эрмит по 4 точкам: между i (задержка whole) и i − 1 (задержка whole + 1).
        const float xm1 = at (channel, i + 1 >= size ? i + 1 - size : i + 1), x0 = at (channel, i),
                    x1 = at (channel, i - 1), x2 = at (channel, i - 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + x0;
    }

    double fs = 44100.0;
    std::array<std::vector<float>, maxChannels> line;
    int size = 0, write = 0;
    double delay = -1.0, pull = -1.0, glide = 0.0;
    float feedback = 0.0f, lastTone = -1.0f;
    Biquad tone;
    int countdown = 0;
};
constexpr double compMaxThresholdDb = -36.0; // порог компрессора при «Сжатии» 100% (0% — порог 0 дБ)
constexpr double compKneeDb = 6.0;           // мягкое колено
constexpr double compMakeupShare = 0.5;      // автоподъём громкости: такая доля подавления, которое получил бы сигнал 0 дБFS

// Розовая машина: компрессор с одним главным регулятором. Каналы сцеплены (общий детектор пика), стерео не плывёт.
// Порог опускается со «Сжатием», громкость поднимается обратно автоматически (половина подавления у 0 дБFS).
class Compressor
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        reset();
    }

    void reset() noexcept
    {
        reduction = 0.0;
        last = { -1.0f, -1.0f, -1.0f, -1.0f };
    }

    // Раз в сэмпл-фрейм: amount 0..1, атака и отпускание в мс, степень (ratio) ≥ 1.
    void setParams (float amount, float attackMs, float releaseMs, float ratio) noexcept
    {
        if (changed (amount, last[0]) || changed (ratio, last[3]))
        {
            threshold = compMaxThresholdDb * static_cast<double> (amount);
            slope = 1.0 - 1.0 / static_cast<double> (ratio);
            makeup = -threshold * slope * compMakeupShare;
        }
        if (changed (attackMs, last[1]))
            attack = 1.0 - std::exp (-1.0 / (0.001 * static_cast<double> (attackMs) * fs));
        if (changed (releaseMs, last[2]))
            release = 1.0 - std::exp (-1.0 / (0.001 * static_cast<double> (releaseMs) * fs));
        last = { amount, attackMs, releaseMs, ratio };
    }

    // Пик фрейма по всем каналам → множитель громкости для этого фрейма.
    float gainFor (float peak) noexcept
    {
        const double over = 20.0 * std::log10 (std::max (1.0e-9, static_cast<double> (peak))) - threshold;
        const double half = compKneeDb * 0.5;
        const double target = over <= -half ? 0.0
                            : over >= half  ? over * slope
                                            : slope * (over + half) * (over + half) / (2.0 * compKneeDb);
        reduction += (target - reduction) * (target > reduction ? attack : release);
        return static_cast<float> (std::pow (10.0, (makeup - reduction) / 20.0));
    }

    double reductionDb() const noexcept { return reduction; } // текущее подавление, дБ ≥ 0

private:
    double fs = 44100.0;
    double threshold = 0.0, slope = 0.75, makeup = 0.0, attack = 1.0, release = 1.0, reduction = 0.0;
    std::array<float, 4> last {};
};

// Зелёная машина: лента, которая тормозит и разгоняется. Головка чтения едет со скоростью speed, запись — всегда 1,
// поэтому при торможении лента отстаёт (lag растёт) и звук опускается вниз по высоте; громкость гаснет у самой остановки.
// Разогнавшись, лента отстаёт на половину разгона — за tapeCatchUpSeconds плавно переходим на живой вход.
class TapeStop
{
public:
    // Единственное место, где выделяется память, — до запуска звука.
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        size = static_cast<int> (std::ceil (tapeMaxSeconds * fs)) + 8;
        for (auto& l : line)
            l.assign (static_cast<size_t> (size), 0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& l : line)
            std::fill (l.begin(), l.end(), 0.0f);
        write = 0;
        lag = 0.0;
        tape = 0.0f;
        gain = 1.0f;
    }

    // Раз в сэмпл-фрейм, до processSample.
    void setSpeed (double speed) noexcept
    {
        if (speed < 1.0)
        {
            lag = std::min (lag + 1.0 - speed, static_cast<double> (size - 4));
            tape = 1.0f; // слышна только лента
        }
        else if (tape > 0.0f)
        {
            tape = std::max (0.0f, tape - static_cast<float> (1.0 / (tapeCatchUpSeconds * fs)));
            if (tape <= 0.0f)
                lag = 0.0; // догнали живой вход
        }
        gain = static_cast<float> (std::min (1.0, speed * 8.0)); // гаснет на последних ~12% скорости
    }

    float processSample (int channel, float x) noexcept
    {
        auto& l = line[static_cast<size_t> (channel)];
        l[static_cast<size_t> (write)] = x;
        if (tape <= 0.0f)
            return x;

        const double pos = static_cast<double> (write) - lag;
        const double whole = std::floor (pos);
        const auto frac = static_cast<float> (pos - whole);
        const auto at = [&] (int i) { return l[static_cast<size_t> (i < 0 ? i + size : i)]; };
        const int i = static_cast<int> (whole);
        const float played = gain * (at (i) + frac * (at (i + 1 >= size ? i + 1 - size : i + 1) - at (i)));
        return x + tape * (played - x);
    }

    // После всех каналов фрейма.
    void advance() noexcept { write = write + 1 < size ? write + 1 : 0; }

private:
    double fs = 44100.0;
    std::array<std::vector<float>, maxChannels> line;
    int size = 0, write = 0;
    double lag = 0.0;
    float tape = 0.0f; // доля ленты в выходе: 1 — тормоз и разгон, дальше спадает к живому входу
    float gain = 1.0f;
};
} // namespace gentra
