cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # статический CRT: плагин не требует VC++ Redistributable

project(SauceGentraFX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.17-security
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(GENTRA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GENTRA_FORMATS AU)
endif()

# На macOS плагины ставятся в ~/Library без прав администратора; на Windows копируем вручную.
option(GENTRA_COPY_AFTER_BUILD "Copy built plugins to the system plugin folders" ${APPLE})

juce_add_plugin(SauceGentraFX
    PRODUCT_NAME "Sauce Gentra FX"
    COMPANY_NAME "Secret Sauce"
    BUNDLE_ID com.secretsauce.gentrafx
    PLUGIN_MANUFACTURER_CODE SCRT
    PLUGIN_CODE GNFX
    FORMATS ${GENTRA_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD ${GENTRA_COPY_AFTER_BUILD})

target_sources(SauceGentraFX PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

# Графика интерфейса: замените файлы в Assets/ своими и пересоберите.
juce_add_binary_data(GentraAssets SOURCES
    Assets/body.png
    Assets/wheel.png
    Assets/traffic1.png
    Assets/traffic2.png
    Assets/traffic3.png)

target_compile_definitions(SauceGentraFX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SauceGentraFX
    PRIVATE
        GentraAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Самопроверка: гоняет настоящий AudioProcessor без хоста. Запуск: ctest --test-dir build
# Кадр интерфейса без DAW: build/GentraTest --snapshot кадр.png [ручки, %] [кадров анимации]
enable_testing()
add_executable(GentraTest Tests/PluginTest.cpp)
target_link_libraries(GentraTest PRIVATE SauceGentraFX juce::juce_audio_utils)
add_test(NAME GentraTest COMMAND GentraTest)
