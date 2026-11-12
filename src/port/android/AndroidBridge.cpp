/**
 * JNI surface for the Android app: menu state, ROM recognition, and extraction.
 *
 * On-screen controls are not here — the engine draws its own, in
 * src/port/ui/TouchControls.cpp.
 *
 * The two extraction entry points take their paths as arguments rather than
 * asking Ship::Context: the launcher calls them before SDL exists.
 */
#ifdef __ANDROID__

#include <jni.h>
#include <SDL2/SDL.h>
#include <android/log.h>

#include <atomic>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <mutex>
#include <libultraship/bridge/consolevariablebridge.h>
#include <exception>
#include <filesystem>
#include <string>

#include "extractor/GameExtractor.h"
#include "ship/Context.h"
#include "ship/window/Window.h"
#include "ship/window/gui/Gui.h"
#include <libultraship/bridge/windowbridge.h>

namespace {

constexpr const char* kLogTag = "Paperboat";
constexpr const char* kGameArchive = "pm64.o2r";
std::atomic<int> menuSoundEvents { 0 };
std::atomic<size_t> extractionCount { 0 };
std::atomic<size_t> extractionTotal { 0 };
std::mutex settingsMutex;
std::string pendingSettings;
std::atomic<bool> settingsDirty { true };

std::string ToStdString(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(value, nullptr);
    std::string result = chars != nullptr ? chars : "";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(value, chars);
    }
    return result;
}

} // namespace

extern "C" {

// The launcher runs in its own process, so settings changed there while the
// game is paused behind it only exist as the pending file. Coming back to the
// foreground asks for that file to be read again.
void PaperboatRecheckPendingSettings() {
    settingsDirty.store(true);
}

void PaperboatApplyPendingSettings() {
    auto context = Ship::Context::GetRawInstance();
    if (!context || !context->GetWindow()) return;
    if (!settingsDirty.exchange(false)) return;
    std::string payload;
    {
        std::lock_guard<std::mutex> guard(settingsMutex);
        payload.swap(pendingSettings);
    }
    const auto path = std::filesystem::path(SDL_AndroidGetExternalStoragePath()) / "settings-pending.txt";
    if (payload.empty()) {
        std::ifstream file(path);
        if (!file) return;
        payload.assign(std::istreambuf_iterator<char>(file), {});
    }
    std::istringstream input(payload);
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find('|'), second = line.find('|', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        const auto key = line.substr(first + 1, second - first - 1);
        if (key.rfind("gSettings.",0) != 0 && key.rfind("gTouchControls.",0) != 0
            && key.rfind("gEnhancements.",0) != 0 && key.rfind("gCheats.",0) != 0
            && key.rfind("gXR.",0) != 0
            && key != "gTextureFilter" && key != "gVsyncEnabled") continue;
        try {
            const float value = std::stof(line.substr(second+1));
            if (line[0] == 'f') CVarSetFloat(key.c_str(), value);
            else CVarSetInteger(key.c_str(), static_cast<int32_t>(value));
            if (key == "gSettings.InternalResolution") context->GetWindow()->SetResolutionMultiplier(value);
            if (key == "gSettings.MSAAValue") context->GetWindow()->SetMsaaLevel(static_cast<int32_t>(value));
            if (key == "gTouchControls.EditMode" && value != 0 && context->GetWindow()->GetGui()->GetMenu())
                context->GetWindow()->GetGui()->GetMenu()->Hide();
        } catch (const std::exception&) { }
    }
    CVarSave();
    std::error_code ignored;
    std::filesystem::remove(path,ignored);
}

JNIEXPORT jfloat JNICALL Java_dev_net64_paperboat_EngineSettings_readValue(
    JNIEnv* env, jobject, jstring jKey, jfloat fallback, jboolean floating) {
    if (Ship::Context::GetRawInstance() == nullptr) return fallback;
    const auto key = ToStdString(env,jKey);
    return floating ? CVarGetFloat(key.c_str(),fallback) : static_cast<float>(CVarGetInteger(key.c_str(),static_cast<int32_t>(fallback)));
}

JNIEXPORT void JNICALL Java_dev_net64_paperboat_EngineSettings_queueChanges(JNIEnv* env, jobject, jstring data) {
    std::lock_guard<std::mutex> guard(settingsMutex);
    pendingSettings = ToStdString(env,data);
    settingsDirty = true;
}

void PaperboatMenuSound(int event) {
    menuSoundEvents.fetch_or(event, std::memory_order_relaxed);
}

JNIEXPORT jint JNICALL Java_dev_net64_paperboat_GameAssets_extractionProgress(JNIEnv*, jobject) {
    const auto total = extractionTotal.load();
    if (total == 0) return 0;
    return static_cast<jint>(std::min<size_t>(99, extractionCount.load() * 100 / total));
}

JNIEXPORT jint JNICALL Java_dev_net64_paperboat_MainActivity_consumeMenuSoundEvents(JNIEnv*, jobject) {
    return menuSoundEvents.exchange(0, std::memory_order_relaxed);
}

JNIEXPORT void JNICALL Java_dev_net64_paperboat_MainActivity_togglePortMenu(JNIEnv*, jobject) {
    // SDL queues the toggle for the game thread; no GUI objects are touched here.
    SDL_Event event {};
    event.type = SDL_KEYDOWN;
    event.key.state = SDL_PRESSED;
    event.key.keysym.scancode = SDL_SCANCODE_F1;
    event.key.keysym.sym = SDLK_F1;
    SDL_PushEvent(&event);
    event.type = SDL_KEYUP;
    event.key.state = SDL_RELEASED;
    SDL_PushEvent(&event);
}

/**
 * Whether libultraship's menu is up. A keyboard, a gamepad or the menu itself
 * can change this behind Kotlin's back, so ask the engine rather than mirror it.
 *
 * Races with the game thread by design: it decides whether one button shows,
 * and a torn read self-corrects on the next poll.
 */
JNIEXPORT jboolean JNICALL Java_dev_net64_paperboat_MainActivity_isMenuOpen(JNIEnv*, jobject) {
    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return JNI_FALSE;
    }

    auto window = context->GetWindow();
    if (window == nullptr) {
        return JNI_FALSE;
    }

    auto gui = window->GetGui();
    if (gui == nullptr) {
        return JNI_FALSE;
    }

    return gui->GetMenuOrMenubarVisible() ? JNI_TRUE : JNI_FALSE;
}

/**
 * The name config.yml gives the ROM at romPath, or null if it has no recipe.
 * Reading the same config.yml the extraction does keeps the launcher from
 * carrying its own copy of the supported hashes.
 */
JNIEXPORT jstring JNICALL
Java_dev_net64_paperboat_GameAssets_nativeDetectRom(JNIEnv* env, jobject, jstring jRomPath, jstring jSourceDir) {
    const std::string romPath = ToStdString(env, jRomPath);
    const std::string sourceDir = ToStdString(env, jSourceDir);

    const auto version = GameExtractor::DetectVersion(romPath, sourceDir);
    if (!version.has_value()) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag, "Unrecognised ROM at %s", romPath.c_str());
        return nullptr;
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Recognised %s as %s", romPath.c_str(), version->c_str());
    return env->NewStringUTF(version->c_str());
}

/**
 * Runs Torch over the ROM at romPath and writes pm64.o2r into destDir.
 * sourceDir holds config.yml and assets/, unpacked from the APK by the
 * launcher. Returns null on success, or a message describing the failure.
 */
JNIEXPORT jstring JNICALL Java_dev_net64_paperboat_GameAssets_nativeGenerateGameArchive(
    JNIEnv* env, jobject, jstring jRomPath, jstring jSourceDir, jstring jDestDir
) {
    const std::string romPath = ToStdString(env, jRomPath);
    const std::string sourceDir = ToStdString(env, jSourceDir);
    const std::string destDir = ToStdString(env, jDestDir);

    const auto fail = [env](const std::string& message) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Asset generation failed: %s", message.c_str());
        return env->NewStringUTF(message.c_str());
    };

    GameExtractor extractor;
    if (!extractor.RunStandalone(romPath, sourceDir)) {
        return fail("Could not read a supported Paper Mario ROM at " + romPath + ".");
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Extracting %s into %s", romPath.c_str(), destDir.c_str());

    std::string extractError;
    extractionCount = 0;
    extractionTotal = 0;
    try {
        if (!extractor.GenerateOTRTo(extractionCount, extractionTotal, sourceDir, destDir)) {
            extractError = GameExtractor::sLastError.empty()
                ? "Torch could not extract the ROM."
                : "Torch could not extract the ROM: " + GameExtractor::sLastError;
        }
    } catch (const std::exception& error) {
        extractError = std::string("Torch could not extract the ROM: ") + error.what();
    } catch (...) {
        extractError = "Torch could not extract the ROM.";
    }

    if (!extractError.empty()) {
        return fail(extractError);
    }

    // Init() logs and returns rather than throwing for some failures, so
    // confirm the archive really landed.
    std::error_code error;
    const std::filesystem::path archive = std::filesystem::path(destDir) / kGameArchive;
    if (!std::filesystem::exists(archive, error) || std::filesystem::file_size(archive, error) == 0) {
        return fail(
            std::string("Torch finished without producing ") + kGameArchive
            + ". Check that the ROM is Paper Mario (N64, US)."
        );
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Wrote %s", archive.c_str());
    return nullptr;
}

} // extern "C"

#endif // __ANDROID__
