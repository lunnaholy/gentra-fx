#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Dsp.h"

class GentraAudioProcessor final : public juce::AudioProcessor
{
public:
    GentraAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Пик низа (< ~150 Гц) на выходе с прошлого вызова — для тряски дороги в интерфейсе.
    float takeBassLevel() noexcept { return bassLevel.exchange (0.0f); }

    // Где сейчас транспорт DAW: доля внутри такта (0 — сильная доля, дробная часть — фаза доли) и темп в долях в минуту.
    // Между блоками досчитывается по темпу. false — транспорт стоит или хост не сообщает позицию.
    bool beatNow (double& beatInBar, double& bpm) const noexcept;

    // Порядок всех машин в гараже (номера gentra::car). Рабочие эффекты звучат в этом порядке, остальные пропускаются.
    // Хранится в состоянии плагина (пресеты); звук переключается на новый порядок через короткий провал громкости.
    // Насколько эффект машины давит громкость сейчас (0..1): сайдчейн — провал, гейт — закрытость. Для визуала.
    float effectLevel (int car) const noexcept;

    using CarOrder = std::array<int, gentra::car::count>;
    static constexpr CarOrder defaultOrder { gentra::car::black, gentra::car::rustCar, gentra::car::red, gentra::car::white,
                                             gentra::car::silver, gentra::car::green, gentra::car::orange, gentra::car::purple,
                                             gentra::car::blue, gentra::car::pink, gentra::car::grey, gentra::car::yellow };
    CarOrder getCarOrder() const;
    void setCarOrder (const CarOrder&);

private:
    // Сглаженные параметры одной машины: главная ручка, до трёх доп. параметров, итоговый wet (Mix с учётом вкл/выкл).
    struct Stage
    {
        enum Index { main, extraA, extraB, extraC, wet, count };

        std::array<std::atomic<float>*, count> raw {}; // для wet — параметр Mix
        std::array<float, count> scale {};
        std::atomic<float>* on = nullptr;
        int aliveMask = 0; // какие из main/extraA/extraB/extraC, отличные от нуля, значат «есть эффект»; 0 — есть всегда
        static constexpr int keep (int index) { return 1 << index; }
        std::array<juce::SmoothedValue<float>, count> value;

        float target (int i) const noexcept;
        void prepare (double sampleRate) noexcept;
        void snap() noexcept;
        void updateTargets() noexcept;
        void skip (int numSamples) noexcept;
        bool active() const noexcept;
        float next (int i) noexcept { return value[static_cast<size_t> (i)].getNextValue(); }
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    Stage makeStage (const char* mainId, float mainScale, const char* aId, float aScale, const char* bId, float bScale,
                     const char* mixId, const char* onId, int aliveMask, const char* cId = nullptr, float cScale = 0.0f);

    // Каждая стадия обрабатывает весь блок на месте; простаивающая не трогает буфер.
    void processCar (int car, juce::AudioBuffer<float>&, int numChannels) noexcept;
    void processBass (juce::AudioBuffer<float>&, int numChannels) noexcept;
    void processLores (juce::AudioBuffer<float>&, int numChannels) noexcept;
    void processDrive (juce::AudioBuffer<float>&, int numChannels) noexcept;
    void processEq (juce::AudioBuffer<float>&, int numChannels) noexcept;
    void processDelay (juce::AudioBuffer<float>&, int numChannels) noexcept;
    // Громкость по фазе шага: shape (phase, stepSeconds, extraA) → сколько убрать 0..1; level — куда опубликовать.
    template <typename Shape>
    void processStepGain (Stage&, std::atomic<float>* note, std::atomic<float>& level, juce::AudioBuffer<float>&, int numChannels, Shape&& shape) noexcept;
    // Стадия на готовом блоке JUCE: параметры — раз в кусок fxChunk, Mix — посэмплово.
    // apply (стерео-блок, main, extraA, extraB) обрабатывает копию входа на 100% wet; reset — когда стадия простаивает.
    template <typename Apply, typename Reset>
    void processChunked (Stage&, juce::AudioBuffer<float>&, int numChannels, Apply&& apply, Reset&& reset) noexcept;

    // Звучащие машины по порядку: номер + 1 в каждых 4 битах, младшие — первая; 0 — конец.
    std::atomic<std::uint64_t> chain { 0 };
    std::uint64_t playingChain = 0; // что звучит сейчас (аудиопоток)
    void storeChain (const CarOrder&) noexcept;
    // Смена порядка: громкость плавно в ноль, новый порядок, плавно обратно.
    // ponytail: провал ~20 мс вместо настоящего кроссфейда (тот требует второй копии всех стадий).
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> chainGain;

    Stage black, rust, red, white, silver, blue, pink, grey, purple, yellow, green, orange;
    std::atomic<float>* sidechainNote; // индекс в gentra::delayDivisionNames
    std::atomic<float>* gateNote;
    std::atomic<float>* tapeCycle; // индекс в gentra::tapeCycleNames
    std::atomic<float>* panNote;
    std::atomic<float>* panShape; // индекс в gentra::panShapeNames
    std::atomic<float> duckLevel { 0.0f }, gateLevel { 0.0f }, tapeLevel { 0.0f }, panLevel { 0.0f };
    // Автопанорама: сглаженное положение, случайное значение текущего цикла и номер цикла, для которого оно выпало.
    double panNow = 0.0, panRandom = 0.0;
    long long panCycle = -1;
    juce::Random panRng;
    void processPan (juce::AudioBuffer<float>&, int numChannels) noexcept;
    gentra::TapeStop tape;
    void processTape (juce::AudioBuffer<float>&, int numChannels) noexcept;
    // Позиция в четвертях: с хоста, пока он играет, иначе бежит сама по последнему темпу.
    double ppqNow = 0.0;
    // Блоки JUCE готовятся на 2 канала и всегда получают стерео-копию (моно — дублируется): их ассерты требуют
    // ровно столько каналов, сколько при prepare.
    static constexpr int fxChunk = 32;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    juce::Reverb reverb;
    juce::dsp::DelayLine<float> preDelay;
    std::atomic<float>* delaySync;
    std::atomic<float>* delayNote; // индекс в gentra::delayDivisionNames
    std::atomic<double> hostBpm { 120.0 }; // последний темп хоста; без хоста — 120
    gentra::Equalizer eq;
    // Частоты полос эквалайзера и Q середины: в Stage места нет (все четыре слота — усиления), сглаживаются здесь.
    static constexpr std::array<const char*, 4> eqShapeIds { "eqLowFreq", "eqMidFreq", "eqHighFreq", "eqMidQ" };
    std::array<std::atomic<float>*, 4> eqShapeRaw {};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, 4> eqShape;
    gentra::Delay delay;
    gentra::BassBoost bass;
    gentra::LoRes lores;
    gentra::Drive drive;
    // ×2, IIR-полуполосные фильтры. Сухой сигнал смешивается внутри — проходит те же фильтры, фазы совпадают.
    // Фильтры крутятся и в обходе (на копии, буфер не трогают): включение не стартует с нулевого состояния.
    static constexpr int driveChunk = 64;
    juce::dsp::Oversampling<float> oversampling { gentra::maxChannels, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    // Переход между обработкой и обходом — кроссфейд: у фильтров оверсэмплинга свой фазовый сдвиг, скачок щёлкал бы.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveEngaged;

    std::atomic<float> bassLevel { 0.0f };

    // Снимок транспорта на начало последнего блока (пишет аудиопоток, читает интерфейс).
    std::atomic<bool> clockPlaying { false };
    std::atomic<double> clockBeat { 0.0 }, clockBpm { 120.0 }, clockStampMs { 0.0 };
    std::atomic<int> clockBeatsPerBar { 4 };
    void readTransport() noexcept;
    float meterLp = 0.0f, meterCoeff = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GentraAudioProcessor)
};
