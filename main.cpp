
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cerrno>

#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <mach-o/dyld.h>
//#include <limits.h>

#include "TTS.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

static size_t writeCallback(
    char* data, size_t size, size_t count, void* userdata)
{
    auto* output = static_cast<std::string*>(userdata);
    size_t bytes = size * count;
    output->append(data, bytes);
    return bytes;
}

std::string readFile(const fs::path& path)
{
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Не найден файл: " + path.string());

    return std::string(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
}

void runCommand(const std::string& command)
{
    int rc = std::system(command.c_str());
    if (rc != 0)
        throw std::runtime_error("Ошибка выполнения команды");
}

std::string askLLM(const json& bms, const std::string& question)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("Не удалось создать CURL");

    json payload = {
        {"model", "local-model"},
        {"stream", false},
        {"temperature", 0},
        {"max_tokens", 120},
        {"messages", json::array({
            {
                {"role", "system"},
                {"content",
                 "Ты голосовой помощник JK BMS. "
                 "Отвечай кратко на русском языке. "
                 "Используй только факты из JSON. "
                 "Не выдумывай параметры и не управляй BMS."}
            },
            {
                {"role", "user"},
                {"content",
                 "Телеметрия BMS:\n" + bms.dump(2) +
                 "\nВопрос пользователя: " + question}
            }
        })}
    };

    std::string body = payload.dump();
    std::string response;

    curl_slist* headers = nullptr;
    headers = curl_slist_append(
        headers, "Content-Type: application/json");

    curl_easy_setopt(
        curl, CURLOPT_URL,
        "http://127.0.0.1:8080/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK)
        throw std::runtime_error(curl_easy_strerror(rc));

    if (status != 200)
        throw std::runtime_error(
            "Ошибка LLM HTTP " + std::to_string(status) +
            ": " + response);

    json result = json::parse(response);
    return result.at("choices").at(0)
                 .at("message").at("content").get<std::string>();
}

bool isLlamaServerRunning() {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL,
                     "http://127.0.0.1:8080/health");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 1000L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    long httpCode = 0;
    CURLcode result = curl_easy_perform(curl);

    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    }

    curl_easy_cleanup(curl);

    return result == CURLE_OK && httpCode >= 200 && httpCode < 500;
}

pid_t llamaServerPid = -1;

bool startLlamaServer() {
    if (isLlamaServerRunning()) {
        std::cout << "llama-server уже запущен. "
                     "Не будем останавливать его при выходе.\n";
        return true;
    }

    llamaServerPid = fork();

    if (llamaServerPid < 0) {
        std::cerr << "Ошибка fork() при запуске llama-server.\n";
        return false;
    }

    if (llamaServerPid == 0) {
        // Дочерний процесс: запускаем сервер.
        chdir("/Users/zay4ik/CLionProjects/BMS_AI/install");

        setenv(
            "DYLD_LIBRARY_PATH",
            "/Users/zay4ik/CLionProjects/BMS_AI/install/llama.cpp/lib",
            1
        );

        execl(
            "./llama.cpp/bin/llama-server",
            "llama-server",
            "-hf", "Qwen/Qwen2.5-1.5B-Instruct-GGUF:Q4_K_M",
            "--host", "127.0.0.1",
            "--port", "8080",
            "-c", "2048",
            "-ngl", "99",
            static_cast<char*>(nullptr)
        );

        // Сюда попадём, только если execl завершился ошибкой.
        perror("Не удалось запустить llama-server");
        _exit(127);
    }

    // Родительский процесс: ждём готовности сервера.
    for (int i = 0; i < 120; ++i) {
        int status = 0;
        pid_t state = waitpid(llamaServerPid, &status, WNOHANG);

        if (state == llamaServerPid) {
            llamaServerPid = -1;
            std::cerr << "llama-server завершился при запуске.\n";
            return false;
        }

        if (isLlamaServerRunning()) {
            std::cout << "llama-server готов к работе.\n";
            return true;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cerr << "llama-server не ответил за 120 секунд.\n";
    return false;
}

void stopLlamaServer() {
    if (llamaServerPid <= 0) {
        return; // Мы не запускали сервер — не останавливаем его.
    }

    std::cout << "\nОстанавливаю llama-server...\n";

    // Просим наш дочерний процесс завершиться.
    kill(llamaServerPid, SIGTERM);

    int status = 0;

    // Ждём завершения процесса, чтобы не оставить zombie-процесс.
    while (waitpid(llamaServerPid, &status, 0) < 0) {
        if (errno != EINTR) {
            break;
        }
    }

    llamaServerPid = -1;
    std::cout << "llama-server остановлен.\n";
}

bool ensureWhisperModel(std::string appDir) {
    const std::string projectDir = appDir;
        //"/Users/zay4ik/CLionProjects/BMS_AI";

    const std::string modelPath =
        projectDir + "/models/ggml-base.bin";

    struct stat info{};

    // Модель уже существует — скачивание не нужно.
    if (stat(modelPath.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
        std::cout << "Модель Whisper уже загружена.\n";
        return true;
    }

    std::cout << "Модель Whisper не найдена. Загружаю base...\n";

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return false;
    }

    if (pid == 0) {
        // Запускаем скрипт из корня проекта.
        if (chdir(projectDir.c_str()) != 0) {
            perror("chdir");
            _exit(1);
        }

        execl(
            "/bin/sh",
            "sh",
            "./models/download-ggml-model.sh",
            "base",
            static_cast<char*>(nullptr)
        );

        perror("execl");
        _exit(127);
    }

    int status = 0;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return false;
        }
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "Ошибка загрузки модели Whisper.\n";
        return false;
    }

    // Проверяем, что файл действительно появился.
    if (stat(modelPath.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
        std::cout << "Модель Whisper успешно загружена.\n";
        return true;
    }

    std::cerr << "Скрипт завершился, но ggml-base.bin не найден.\n";
    return false;
}

namespace fs = std::filesystem;

fs::path getExecutableDir() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::string buffer(size, '\0');

    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        throw std::runtime_error("Не удалось определить путь приложения");
    }

    fs::path executable = fs::canonical(buffer.c_str());
    return executable.parent_path();
}

int main()
{
    std::atexit(stopLlamaServer);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    const fs::path appDir = getExecutableDir();

    if (!ensureWhisperModel(appDir.string())) {
        return 1;
    }

    if (!startLlamaServer()) {
        curl_global_cleanup();
        return 1;
    }

    //const fs::path home = std::getenv("HOME");
    const fs::path project = appDir; //home / "CLionProjects" / "BMS_AI";
    const fs::path whisper = appDir / "whisper-cli"; //home / project / "whisper.cpp";

    const fs::path wav = appDir / "question.wav"; //project / "question.wav";
    const fs::path transcript = appDir / "transcript.txt"; //project / "transcript.txt";
    const fs::path answer = appDir / "answer.txt"; //project / "answer.txt";

    // Только демонстрационные данные!
    json bms = {
        {"soc_percent", 82},
        {"voltage_v", 52.4},
        {"current_a", -12.5},
        {"temperature_c", 27.3},
        {"alarms", json::array()}
    };

    try {
        while (true) {
            std::cout << "\nНажми Enter и задай вопрос "
                         "(напиши exit для выхода): "
                      << std::flush;

            std::string input;
            std::getline(std::cin, input);

            if (input == "exit")
                break;

            // Записываем 6 секунд с микрофона.
            runCommand(
                "ffmpeg -y -v error "
                "-f avfoundation -i ':0' -t 6 "
                "-ar 16000 -ac 1 -c:a pcm_s16le '" +
                wav.string() + "'");

            // Распознаём русский текст.
            runCommand(
                // "cd '" + whisper.string() +
                // "' && ./build/bin/whisper-cli "
                "DYLD_LIBRARY_PATH=" + appDir.string() + "/../install/whisper.cpp/lib " + whisper.string() +
                " -m models/ggml-base.bin "
                "-f '" + wav.string() +
                "' -l ru -otxt -of '" +
                (project / "transcript").string() + "'");

            std::string question = readFile(transcript);

            if (question.empty()) {
                std::cout << "Не удалось распознать речь.\n";
                continue;
            }

            std::cout << "Распознано: " << question << '\n';

            std::string reply = askLLM(bms, question);
            std::cout << "Помощник: " << reply << '\n';

            {
                std::ofstream out(answer);
                if (!out)
                    throw std::runtime_error("Не удалось сохранить ответ");
                out << reply;
            }

            // Озвучивание локальным синтезатором sherpa.
            speak(appDir, reply);
            // Озвучивание локальным синтезатором macOS.
            //runCommand("say -f '" + answer.string() + "'");
        }
    } catch (const std::exception& e) {
        std::cerr << "\nОшибка: " << e.what() << '\n';
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();
    return 0;
}

