#include <espeak-ng/speak_lib.h>
#include "piper-phonemize/src/phonemize.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main() {
    const std::string dataDir =
        std::string(std::getenv("HOME")) +
        "/CLionProjects/BMS_AI/piper-phonemize/build/ei/share/espeak-ng-data";

    int result = espeak_Initialize(
        AUDIO_OUTPUT_SYNCHRONOUS, 0, dataDir.c_str(), 0);

    if (result < 0) {
        std::cerr << "Ошибка инициализации eSpeak-ng\n";
        return 1;
    }

    piper::eSpeakPhonemeConfig config;
    config.voice = "ru";

    std::vector<std::vector<piper::Phoneme>> phonemes;

    try {
        piper::phonemize_eSpeak(
            "Привет, заряд батареи составляет 82 процента.",
            config, phonemes);
    } catch (const std::exception& e) {
        std::cerr << "Ошибка фонемизации: " << e.what() << '\n';
        return 2;
    }

    for (const auto& sentence : phonemes) {
        std::cout << "Кодовые точки:";
        for (auto cp : sentence) {
            std::cout << ' ' << static_cast<uint32_t>(cp);
        }
        std::cout << '\n';
    }

    std::cout << "Количество фрагментов: "
              << phonemes.size() << '\n';

    return phonemes.empty() ? 3 : 0;
}