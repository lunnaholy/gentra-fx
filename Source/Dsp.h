#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// DSP без зависимости от JUCE. Все amount — в диапазоне 0..1.
namespace gentra
{
constexpr int maxChannels = 2;

// ---- Константы для подстройки на слух ----
constexpr double bassMaxDb     = 12.0;   // усиление шельфа при 100%
constexpr double bassFreqHz    = 100.0;  // частота шельфа
constexpr double bassQ         = 0.707;  // без резонансного горба
constexpr float  clipThreshold = 0.8f;   // ниже порога клиппер ничего не делает (~ -2 dBFS)
constexpr float  clipFadeIn    = 0.05f;  // клиппер плавно включается на первых 5% хода ручки
constexpr double loresMinHz    = 1500.0; // частота sample-and-hold при 100%
constexpr double loresMaxBits  = 16.0;   // битность при 0%
constexpr double loresMinBits  = 4.0;    // битность при 100%

// Линейно до порога, выше — tanh-колено; выход всегда в [-1, 1].
inline float softClip (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= clipThreshold)
        return x;

    const float knee = 1.0f - clipThreshold;
    return std::copysign (clipThreshold + knee * std::tanh ((a - clipThreshold) / knee), x);
}

// Low-shelf (RBJ, TDF-II) + мягкий клиппер.
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
        z1 = {};
        z2 = {};
        coeffAmount = -1.0f;
        countdown = 0;
    }

    // Вызывать раз в сэмпл-фрейм; коэффициенты пересчитываются не чаще раза в 16 сэмплов.
    void setAmount (float amount) noexcept
    {
        clipMix = std::min (1.0f, amount / clipFadeIn);

        if (countdown > 0)
            --countdown;

        if (countdown == 0 && std::abs (amount - coeffAmount) > 0.0f)
        {
            updateCoefficients (amount);
            countdown = 16;
        }
    }

    float processSample (int channel, float x) noexcept
    {
        const auto ch = static_cast<size_t> (channel);
        const double in = x;
        const double out = b0 * in + z1[ch];
        z1[ch] = b1 * in - a1 * out + z2[ch];
        z2[ch] = b2 * in - a2 * out;

        const auto y = static_cast<float> (out);
        return y + clipMix * (softClip (y) - y);
    }

private:
    void updateCoefficients (float amount) noexcept
    {
        coeffAmount = amount;

        const double pi = 3.14159265358979323846;
        const double A = std::pow (10.0, bassMaxDb * static_cast<double> (amount) / 40.0);
        const double w0 = 2.0 * pi * bassFreqHz / fs;
        const double cosw = std::cos (w0);
        const double k = 2.0 * std::sqrt (A) * std::sin (w0) / (2.0 * bassQ);
        const double a0 = (A + 1.0) + (A - 1.0) * cosw + k;

        b0 = A * ((A + 1.0) - (A - 1.0) * cosw + k) / a0;
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw) / a0;
        b2 = A * ((A + 1.0) - (A - 1.0) * cosw - k) / a0;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw) / a0;
        a2 = ((A + 1.0) + (A - 1.0) * cosw - k) / a0;
    }

    double fs = 44100.0;
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    std::array<double, maxChannels> z1 {}, z2 {};
    float coeffAmount = -1.0f;
    float clipMix = 0.0f;
    int countdown = 0;
};

// Sample-and-hold с дробным шагом + квантование (mid-tread: тишина остаётся тишиной).
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
        amount = -1.0f;
    }

    void setAmount (float newAmount) noexcept
    {
        if (std::abs (newAmount - amount) > 0.0f)
        {
            amount = newAmount;
            const auto t = static_cast<double> (newAmount);
            // Целевая частота: лог-интерполяция от fs (0%) до loresMinHz (100%).
            increment = std::min (1.0, std::pow (loresMinHz / fs, t));
            const double bits = loresMaxBits + (loresMinBits - loresMaxBits) * t;
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
    float amount = -1.0f;
};
} // namespace gentra
