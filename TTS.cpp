#include "TTS.h"

#include <sherpa-onnx/c-api/cxx-api.h>
#include <piper-phonemize/phonemize.hpp>
#include <espeak-ng/speak_lib.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sherpa_onnx::cxx;

// const std::string projectDir =
//     std::string(std::getenv("HOME")) + "/CLionProjects/BMS_AI";

// const std::string piperDataDir =
//     projectDir + "/piper-phonemize/build/ei/share/espeak-ng-data";
// const std::string piperDataDir =
//     projectDir + "/install/piper_phonemize/share/espeak-ng-data";

void initializePhonemizer(const std::string& appDir) {
    static std::once_flag flag;

    const std::string piperDataDir = appDir + "/../install/piper_phonemize/share/espeak-ng-data";
    std::call_once(flag, [&] {
        int result = espeak_Initialize(
            AUDIO_OUTPUT_SYNCHRONOUS, 0, piperDataDir.c_str(), 0);

        if (result < 0) {
            throw std::runtime_error(
                "Не удалось инициализировать eSpeak-ng");
        }
    });
}

OfflineTts& getTts(const std::string& appDir) {
    static OfflineTts tts = [&] {
        OfflineTtsConfig config;

        const std::string modelDir =
            appDir + "/piper_irina_model";

        config.model.vits.model =
            modelDir + "/ru_RU-irina-medium.onnx";
        config.model.vits.tokens =
            modelDir + "/tokens.txt";
        config.model.vits.data_dir =
            modelDir + "/espeak-ng-data";

        config.model.num_threads = 4;
        config.model.debug = 0;

        return OfflineTts::Create(config);
    }();

    return tts;
}

std::vector<std::vector<int32_t>> makePhonemeCodepoints(const std::string& appDir,
    const std::string& text) {

    initializePhonemizer(appDir);

    piper::eSpeakPhonemeConfig config;
    config.voice = "ru";

    std::vector<std::vector<piper::Phoneme>> phonemes;
    piper::phonemize_eSpeak(text, config, phonemes);

    std::vector<std::vector<int32_t>> result;
    result.reserve(phonemes.size());

    for (const auto& sentence : phonemes) {
        std::vector<int32_t> codepoints;
        codepoints.reserve(sentence.size());

        for (piper::Phoneme cp : sentence) {
            codepoints.push_back(static_cast<int32_t>(cp));
        }

        result.push_back(std::move(codepoints));
    }

    return result;
}

}  // namespace

void speak(const std::string& appDir, const std::string& text) {
    if (text.empty()) {
        return;
    }

    try {
        auto codepoints = makePhonemeCodepoints(appDir, text);

        if (codepoints.empty()) {
            std::cerr << "Фонемизатор не вернул результат\n";
            return;
        }

        auto& tts = getTts(appDir);

        GenerationConfig gen;
        gen.sid = 0;
        gen.speed = 1.0f;
        gen.phoneme_codepoints = std::move(codepoints);

        auto audio = tts.Generate(text, gen);

        if (audio.samples.empty() || audio.sample_rate <= 0) {
            std::cerr << "TTS не сгенерировал аудио\n";
            return;
        }

        const std::string wav = "/tmp/bms_tts.wav";
        WriteWave(wav, {audio.samples, audio.sample_rate});

        const int result = std::system("afplay /tmp/bms_tts.wav");

        if (result != 0) {
            std::cerr << "Не удалось воспроизвести WAV\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка TTS: " << e.what() << '\n';
    }
}