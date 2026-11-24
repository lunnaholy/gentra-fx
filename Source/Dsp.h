#pragma once

#include <algorithm>
#include <array>
#include <cmath>

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
constexpr double toneBrightHz  = 16000.0; // срез тона при 100%

constexpr double pi = 3.14159265358979323846;

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
} // namespace gentra
