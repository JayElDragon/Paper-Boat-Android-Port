/**
 * OpenXR presentation for headsets (Quest, Pico and other OpenXR Android
 * runtimes): the finished frame is shown on a large virtual screen floating in
 * front of the player, and the motion controllers are exposed to the engine as
 * an ordinary gamepad.
 *
 * Two ways of showing the game:
 *
 *  - Immersive (default): the renderer draws the frame once per eye
 *    (Fast::gStereoHooks), with the head pose placed in the game camera's own
 *    space. The head starts where the game camera is, and moving or turning it
 *    moves the view through the scene in all six degrees of freedom. HUD and
 *    text sit on a plane where the flat screen would be. Submitted as a
 *    projection layer.
 *  - Virtual screen: the finished flat frame is copied out of the default
 *    framebuffer and shown on a quad floating in front of the player. Used
 *    when immersive is switched off and whenever the engine's menu is open.
 *
 * libultraship calls Present() where it would otherwise swap the window. On a
 * device with no OpenXR runtime the instance never comes up and Present()
 * returns false forever, so phones and handhelds are unaffected.
 *
 * Controllers go in through an SDL virtual game controller, so the engine's
 * existing mapping, rebinding and rumble-less input paths apply unchanged.
 */
#ifdef __ANDROID__

#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <SDL2/SDL.h>

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <libultraship/bridge/consolevariablebridge.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

#include <fast/stereo.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include "OpenXR.h"

extern "C" void PaperboatRecheckPendingSettings();
// The game side (xr_game.c) and the engine's interpolation counters.
extern "C" int32_t PaperboatXR_GetPlayerEye(float* out);
extern "C" int32_t PaperboatXR_HasHammer(void);
extern "C" volatile int32_t gPaperboatXRSwingFrames;
extern "C" void PaperboatXR_GetCameraView(float out[4][4]);
extern "C" volatile int32_t gPaperboatXRThrowFlying;
extern "C" volatile float gPaperboatXRThrowPos[3];
extern "C" volatile int32_t gPaperboatXRThrowLanded;
extern "C" int gPaperboatInterpIndex;
extern "C" int gPaperboatInterpCount;

// The hook libultraship's SDL window backend calls instead of swapping.
namespace Fast {
extern bool (*gPresentHook)(SDL_Window* window);
}

namespace {

constexpr const char* kLogTag = "PaperboatXR";
#define XR_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define XR_LOGW(...) __android_log_print(ANDROID_LOG_WARN, kLogTag, __VA_ARGS__)

// Screen placement, in metres. Adjustable live through the CVars.
constexpr const char* kCVarEnabled = "gXR.Enabled";
constexpr const char* kCVarDistance = "gXR.ScreenDistance";
constexpr const char* kCVarWidth = "gXR.ScreenWidth";
constexpr const char* kCVarHeight = "gXR.ScreenHeight";
constexpr float kDefaultDistance = 2.5f;
constexpr float kDefaultWidth = 3.6f;
constexpr float kDefaultHeightOffset = 0.0f;

// Immersive mode. The scale is game units per metre: at 150 the usual camera
// distance puts the scene about three metres away, as a diorama.
constexpr const char* kCVarImmersive = "gXR.Immersive";
constexpr const char* kCVarWorldScale = "gXR.WorldScale";
constexpr const char* kCVarHudDistance = "gXR.HudDistance";
constexpr const char* kCVarBackdropDistance = "gXR.BackdropDistance";
constexpr const char* kCVarLevelHorizon = "gXR.LevelHorizon";
// 0 = the game's own limits, 1 = far, 2 = maximum.
constexpr const char* kCVarDrawDistance = "gXR.DrawDistance";
constexpr float kDefaultWorldScale = 150.0f;
// First person (gXR.Immersive = 2): life size, at about 25 units to the metre
// Mario stands a metre and a half tall.
constexpr const char* kCVarFirstPersonScale = "gXR.FirstPersonScale";
// Smooth turning with the right stick, in degrees a second at full tilt.
// First person: a hammer in the right hand, swung to use Mario's.
constexpr const char* kCVarHammer = "gXR.Hammer";
constexpr const char* kCVarSmoothTurn = "gXR.SmoothTurn";
constexpr const char* kCVarTurnSpeed = "gXR.TurnSpeed";
constexpr float kDefaultTurnSpeed = 90.0f;
constexpr float kDefaultFirstPersonScale = 25.0f;
constexpr float kDefaultHudDistance = 1.8f;
constexpr float kDefaultBackdropDistance = 40.0f;

enum class Phase { Untried, Ready, Unavailable };

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    bool srgb = false;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> framebuffers;
};

struct Actions {
    XrActionSet set = XR_NULL_HANDLE;
    XrAction leftStick = XR_NULL_HANDLE;
    XrAction rightStick = XR_NULL_HANDLE;
    XrAction leftTrigger = XR_NULL_HANDLE;
    XrAction rightTrigger = XR_NULL_HANDLE;
    XrAction leftGrip = XR_NULL_HANDLE;
    XrAction rightGrip = XR_NULL_HANDLE;
    XrAction a = XR_NULL_HANDLE;
    XrAction b = XR_NULL_HANDLE;
    XrAction x = XR_NULL_HANDLE;
    XrAction y = XR_NULL_HANDLE;
    XrAction menu = XR_NULL_HANDLE;
    XrAction leftStickClick = XR_NULL_HANDLE;
    XrAction rightStickClick = XR_NULL_HANDLE;
    XrAction rightGripPose = XR_NULL_HANDLE;
};

Phase sPhase = Phase::Untried;
XrInstance sInstance = XR_NULL_HANDLE;
XrSystemId sSystem = XR_NULL_SYSTEM_ID;
XrSession sSession = XR_NULL_HANDLE;
XrSpace sLocalSpace = XR_NULL_HANDLE;
XrSessionState sSessionState = XR_SESSION_STATE_UNKNOWN;
XrEnvironmentBlendMode sBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
bool sSessionRunning = false;
int32_t sMaxSwapchainWidth = 4096;
int32_t sMaxSwapchainHeight = 4096;
Swapchain sSwapchain;
Actions sActions;

// Immersive frame state. A frame is begun either by the renderer asking for
// eye passes or, failing that, by Present(); Present() always ends it.
Swapchain sEyeSwapchains[2];
int32_t sEyeWidth = 1440;
int32_t sEyeHeight = 1584;
bool sFrameBegun = false;
XrFrameState sFrameState { XR_TYPE_FRAME_STATE };
XrView sViews[2] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
bool sEyeRendered[2] = { false, false };
float sWorldScale = kDefaultWorldScale;
// The draw distance setting while the frame is going out per eye, else 0.
std::atomic<int> sDrawDistance { 0 };
// First person: Mario's eye position at the last two game frames. The world
// is drawn interpolated between game frames, so the anchor has to be too, or
// everything would shudder against it whenever Mario moves.
std::atomic<bool> sFirstPerson { false };
float sEyePrevious[3] = { 0.0f, 0.0f, 0.0f };
float sEyeCurrent[3] = { 0.0f, 0.0f, 0.0f };
bool sEyeKnown = false;

// The hammer hand: where the right controller is this frame, and the GL
// objects the hammer is drawn with.
XrSpace sRightGripSpace = XR_NULL_HANDLE;
bool sHandValid = false;
XrPosef sHandPose {};
Uint64 sLastSwingTicks = 0;
// A throw: squeeze the grip, swing, let go. The hammer then flies on its own
// in the tracking space until the game says it hit something or time runs out.
bool sGripWasHeld = false;
bool sThrowFlying = false;
float sThrowPosition[3] = { 0.0f, 0.0f, 0.0f };
float sThrowVelocity[3] = { 0.0f, 0.0f, 0.0f };
XrQuaternionf sThrowOrientation { 0.0f, 0.0f, 0.0f, 1.0f };
Uint64 sThrowStartTicks = 0;
Uint64 sThrowLastTicks = 0;
// The interpolated first-person anchor the current frame is drawn with.
float sAnchorNow[3] = { 0.0f, 0.0f, 0.0f };
GLuint sHammerProgram = 0;
GLuint sHammerVertexArray = 0;
GLuint sHammerBuffer = 0;
GLint sHammerMvpUniform = -1;
GLint sHammerNormalUniform = -1;
GLint sHammerColorUniform = -1;
void CreateHammer();
void UpdateThrow(Uint64 now);
void PublishThrowPosition();
void EndThrow();

// Smooth turning: the world as the player has turned it, v_tracking =
// R(yaw) * v_game + offset (metres). The offset keeps the turn pivoting on
// wherever the head is at the time rather than on the tracking origin.
std::atomic<bool> sImmersiveLive { false };
float sTurnYaw = 0.0f;
float sTurnOffset[3] = { 0.0f, 0.0f, 0.0f };
Uint64 sTurnLastTicks = 0;

bool SmoothTurnActive() {
    return sImmersiveLive.load(std::memory_order_relaxed) && CVarGetInteger(kCVarSmoothTurn, 0) != 0;
}

/** Turns the world by the right stick's tilt, about the head. */
void UpdateTurn(float stickX) {
    const Uint64 now = SDL_GetTicks64();
    const float seconds = sTurnLastTicks == 0 ? 0.0f : std::min(0.1f, static_cast<float>(now - sTurnLastTicks) / 1000.0f);
    sTurnLastTicks = now;

    const float deadzone = 0.2f;
    const float magnitude = SDL_fabsf(stickX);
    if (magnitude <= deadzone || seconds <= 0.0f) {
        return;
    }
    const float tilt = (magnitude - deadzone) / (1.0f - deadzone) * (stickX < 0.0f ? -1.0f : 1.0f);
    const float speed = std::clamp(CVarGetFloat(kCVarTurnSpeed, kDefaultTurnSpeed), 10.0f, 360.0f);
    // Stick right turns the player right, so the world goes round to the left.
    const float delta = tilt * speed * seconds * 3.14159265f / 180.0f;

    const float headX = (sViews[0].pose.position.x + sViews[1].pose.position.x) * 0.5f;
    const float headZ = (sViews[0].pose.position.z + sViews[1].pose.position.z) * 0.5f;
    const float c = SDL_cosf(delta), s = SDL_sinf(delta);
    const float x = sTurnOffset[0] - headX, z = sTurnOffset[2] - headZ;
    sTurnOffset[0] = x * c + z * s + headX;
    sTurnOffset[2] = -x * s + z * c + headZ;
    sTurnYaw = SDL_fmodf(sTurnYaw + delta, 2.0f * 3.14159265f);
}
std::atomic<bool> sPresenting { false };

// Copy pass: default framebuffer -> sCopyTexture -> swapchain image.
GLuint sCopyFramebuffer = 0;
GLuint sCopyTexture = 0;
int32_t sCopyWidth = 0;
int32_t sCopyHeight = 0;
GLuint sProgram = 0;
GLuint sVertexArray = 0;
GLint sLinearizeUniform = -1;

SDL_Joystick* sVirtualPad = nullptr;
int sVirtualPadIndex = -1;
// The same device, opened as a controller for the menu to navigate with.
SDL_GameController* sVirtualController = nullptr;
bool sMenuPadBound = false;

bool Check(XrResult result, const char* what) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    char text[XR_MAX_RESULT_STRING_SIZE] = "?";
    if (sInstance != XR_NULL_HANDLE) {
        xrResultToString(sInstance, result, text);
    }
    XR_LOGW("%s failed: %s (%d)", what, text, static_cast<int>(result));
    return false;
}

template <typename Fn> bool GetFunction(const char* name, Fn* out) {
    return XR_SUCCEEDED(xrGetInstanceProcAddr(sInstance, name, reinterpret_cast<PFN_xrVoidFunction*>(out))) &&
           *out != nullptr;
}

// ---------------------------------------------------------------- actions --

XrAction MakeAction(XrActionType type, const char* name, const char* label) {
    XrActionCreateInfo info { XR_TYPE_ACTION_CREATE_INFO };
    info.actionType = type;
    SDL_strlcpy(info.actionName, name, sizeof(info.actionName));
    SDL_strlcpy(info.localizedActionName, label, sizeof(info.localizedActionName));
    XrAction action = XR_NULL_HANDLE;
    Check(xrCreateAction(sActions.set, &info, &action), name);
    return action;
}

void Suggest(const char* profile, const std::vector<std::pair<XrAction, const char*>>& wanted) {
    XrPath profilePath = XR_NULL_PATH;
    if (XR_FAILED(xrStringToPath(sInstance, profile, &profilePath))) {
        return;
    }
    std::vector<XrActionSuggestedBinding> bindings;
    for (const auto& [action, path] : wanted) {
        XrPath xrPath = XR_NULL_PATH;
        if (action != XR_NULL_HANDLE && XR_SUCCEEDED(xrStringToPath(sInstance, path, &xrPath))) {
            bindings.push_back({ action, xrPath });
        }
    }
    XrInteractionProfileSuggestedBinding suggestion { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggestion.interactionProfile = profilePath;
    suggestion.suggestedBindings = bindings.data();
    suggestion.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    // A runtime may reject a profile it does not know; the others still apply.
    Check(xrSuggestInteractionProfileBindings(sInstance, &suggestion), profile);
}

void CreateActions() {
    XrActionSetCreateInfo setInfo { XR_TYPE_ACTION_SET_CREATE_INFO };
    SDL_strlcpy(setInfo.actionSetName, "gameplay", sizeof(setInfo.actionSetName));
    SDL_strlcpy(setInfo.localizedActionSetName, "Gameplay", sizeof(setInfo.localizedActionSetName));
    if (!Check(xrCreateActionSet(sInstance, &setInfo, &sActions.set), "xrCreateActionSet")) {
        return;
    }

    sActions.leftStick = MakeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "left_stick", "Left Stick");
    sActions.rightStick = MakeAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "right_stick", "Right Stick");
    sActions.leftTrigger = MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "left_trigger", "Left Trigger");
    sActions.rightTrigger = MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "right_trigger", "Right Trigger");
    sActions.leftGrip = MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "left_grip", "Left Grip");
    sActions.rightGrip = MakeAction(XR_ACTION_TYPE_FLOAT_INPUT, "right_grip", "Right Grip");
    sActions.a = MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a", "A");
    sActions.b = MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b", "B");
    sActions.x = MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x", "X");
    sActions.y = MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y", "Y");
    sActions.menu = MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_menu", "Start");
    sActions.leftStickClick = MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "left_stick_click", "Open Launcher");
    sActions.rightStickClick = MakeAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "right_stick_click", "Open Menu");
    sActions.rightGripPose = MakeAction(XR_ACTION_TYPE_POSE_INPUT, "right_grip_pose", "Hammer Hand");

    Suggest("/interaction_profiles/oculus/touch_controller",
            {
                { sActions.leftStick, "/user/hand/left/input/thumbstick" },
                { sActions.rightStick, "/user/hand/right/input/thumbstick" },
                { sActions.leftTrigger, "/user/hand/left/input/trigger/value" },
                { sActions.rightTrigger, "/user/hand/right/input/trigger/value" },
                { sActions.leftGrip, "/user/hand/left/input/squeeze/value" },
                { sActions.rightGrip, "/user/hand/right/input/squeeze/value" },
                { sActions.a, "/user/hand/right/input/a/click" },
                { sActions.b, "/user/hand/right/input/b/click" },
                { sActions.x, "/user/hand/left/input/x/click" },
                { sActions.y, "/user/hand/left/input/y/click" },
                { sActions.menu, "/user/hand/left/input/menu/click" },
                { sActions.leftStickClick, "/user/hand/left/input/thumbstick/click" },
                { sActions.rightStickClick, "/user/hand/right/input/thumbstick/click" },
                { sActions.rightGripPose, "/user/hand/right/input/grip/pose" },
            });

    // Every runtime has to accept this one; it is only enough to get through
    // menus, but it keeps an unknown controller from being entirely dead.
    Suggest("/interaction_profiles/khr/simple_controller",
            {
                { sActions.a, "/user/hand/right/input/select/click" },
                { sActions.b, "/user/hand/left/input/select/click" },
                { sActions.menu, "/user/hand/left/input/menu/click" },
                { sActions.rightGripPose, "/user/hand/right/input/grip/pose" },
            });
}

void AttachActions() {
    if (sActions.set == XR_NULL_HANDLE) {
        return;
    }
    XrSessionActionSetsAttachInfo attach { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attach.countActionSets = 1;
    attach.actionSets = &sActions.set;
    Check(xrAttachSessionActionSets(sSession, &attach), "xrAttachSessionActionSets");
}

bool ReadBool(XrAction action) {
    if (action == XR_NULL_HANDLE) {
        return false;
    }
    XrActionStateGetInfo get { XR_TYPE_ACTION_STATE_GET_INFO };
    get.action = action;
    XrActionStateBoolean state { XR_TYPE_ACTION_STATE_BOOLEAN };
    return XR_SUCCEEDED(xrGetActionStateBoolean(sSession, &get, &state)) && state.isActive && state.currentState;
}

float ReadFloat(XrAction action) {
    if (action == XR_NULL_HANDLE) {
        return 0.0f;
    }
    XrActionStateGetInfo get { XR_TYPE_ACTION_STATE_GET_INFO };
    get.action = action;
    XrActionStateFloat state { XR_TYPE_ACTION_STATE_FLOAT };
    if (XR_FAILED(xrGetActionStateFloat(sSession, &get, &state)) || !state.isActive) {
        return 0.0f;
    }
    return state.currentState;
}

XrVector2f ReadStick(XrAction action) {
    if (action == XR_NULL_HANDLE) {
        return { 0.0f, 0.0f };
    }
    XrActionStateGetInfo get { XR_TYPE_ACTION_STATE_GET_INFO };
    get.action = action;
    XrActionStateVector2f state { XR_TYPE_ACTION_STATE_VECTOR2F };
    if (XR_FAILED(xrGetActionStateVector2f(sSession, &get, &state)) || !state.isActive) {
        return { 0.0f, 0.0f };
    }
    return state.currentState;
}

Sint16 StickAxis(float value) {
    return static_cast<Sint16>(std::clamp(value, -1.0f, 1.0f) * 32767.0f);
}

// A virtual controller's triggers rest at the axis minimum, as real ones do.
Sint16 TriggerAxis(float value) {
    return static_cast<Sint16>(-32768.0f + std::clamp(value, 0.0f, 1.0f) * 65535.0f);
}

bool sMenuButtonHeld = false;
bool HammerInPlay();
bool sLauncherButtonHeld = false;

void ReleaseVirtualPad();
bool MenuVisible();

/** Asks MainActivity to show the launcher over the still-running game. */
void OpenLauncher() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || activity == nullptr) {
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = env->GetMethodID(activityClass, "openLauncherFromEngine", "()V");
    if (method != nullptr) {
        ReleaseVirtualPad();
        env->CallVoidMethod(activity, method);
        XR_LOGI("Launcher button: launcher requested");
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        XR_LOGW("Launcher button: MainActivity.openLauncherFromEngine is missing or failed");
    }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
}

/**
 * Hands the virtual controller to the menu's UI. It only looks for gamepads
 * when told one was plugged in, and the engine keeps those events to itself,
 * so a controller that appears after start-up (as this one does) is never
 * picked up on its own. Render thread only.
 */
void BindMenuPad() {
    if (sMenuPadBound || sVirtualController == nullptr || ImGui::GetCurrentContext() == nullptr ||
        ImGui::GetIO().BackendPlatformUserData == nullptr) {
        return;
    }
    ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_Manual, &sVirtualController, 1);
    sMenuPadBound = true;
    XR_LOGI("Menu navigation bound to the headset controllers");
}

void UnbindMenuPad() {
    if (!sMenuPadBound) {
        return;
    }
    sMenuPadBound = false;
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendPlatformUserData != nullptr) {
        ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoFirst, nullptr, 0);
    }
}

/**
 * Opens or closes the engine's settings menu. Toggled directly: this runs on
 * the render thread, between frames, as the on-screen menu button does. A
 * headset has no pointer, so controller navigation is switched on with it.
 */
void ToggleEngineMenu() {
    auto context = Ship::Context::GetRawInstance();
    auto window = context != nullptr ? context->GetWindow() : nullptr;
    auto gui = window != nullptr ? window->GetGui() : nullptr;
    auto menu = gui != nullptr ? gui->GetMenu() : nullptr;
    if (menu == nullptr) {
        XR_LOGW("Menu button: the engine menu does not exist yet");
        return;
    }
    CVarSetInteger(CVAR_IMGUI_CONTROLLER_NAV, 1);
    BindMenuPad();
    menu->ToggleVisibility();
    XR_LOGI("Menu button: menu %s", menu->IsVisible() ? "opened" : "closed");
}

void OpenVirtualPad() {
    if (sVirtualPad != nullptr) {
        return;
    }
    if (SDL_WasInit(SDL_INIT_JOYSTICK) == 0) {
        SDL_InitSubSystem(SDL_INIT_JOYSTICK);
    }
    sVirtualPadIndex =
        SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0);
    if (sVirtualPadIndex < 0) {
        XR_LOGW("No virtual controller: %s", SDL_GetError());
        return;
    }
    sVirtualPad = SDL_JoystickOpen(sVirtualPadIndex);
    if (sVirtualPad == nullptr) {
        XR_LOGW("Virtual controller would not open: %s", SDL_GetError());
        SDL_JoystickDetachVirtual(sVirtualPadIndex);
        sVirtualPadIndex = -1;
        return;
    }
    sVirtualController = SDL_GameControllerOpen(sVirtualPadIndex);
    if (sVirtualController == nullptr) {
        XR_LOGW("Virtual controller is not a game controller: %s", SDL_GetError());
    }
}

void CloseVirtualPad() {
    UnbindMenuPad();
    if (sVirtualController != nullptr) {
        SDL_GameControllerClose(sVirtualController);
        sVirtualController = nullptr;
    }
    if (sVirtualPad != nullptr) {
        SDL_JoystickClose(sVirtualPad);
        sVirtualPad = nullptr;
    }
    if (sVirtualPadIndex >= 0) {
        SDL_JoystickDetachVirtual(sVirtualPadIndex);
        sVirtualPadIndex = -1;
    }
}

void ReleaseVirtualPad() {
    if (sVirtualPad == nullptr) {
        return;
    }
    for (int axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; axis++) {
        const bool trigger = axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        SDL_JoystickSetVirtualAxis(sVirtualPad, axis, trigger ? SDL_JOYSTICK_AXIS_MIN : 0);
    }
    for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++) {
        SDL_JoystickSetVirtualButton(sVirtualPad, button, 0);
    }
}

void PumpInput() {
    if (sVirtualPad == nullptr || sActions.set == XR_NULL_HANDLE) {
        return;
    }
    // Input is only delivered while focused; let go of everything otherwise so
    // nothing stays held under the system menu.
    if (sSessionState != XR_SESSION_STATE_FOCUSED) {
        ReleaseVirtualPad();
        sMenuButtonHeld = false;
        sLauncherButtonHeld = false;
        return;
    }

    XrActiveActionSet active { sActions.set, XR_NULL_PATH };
    XrActionsSyncInfo sync { XR_TYPE_ACTIONS_SYNC_INFO };
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_FAILED(xrSyncActions(sSession, &sync))) {
        return;
    }

    const XrVector2f left = ReadStick(sActions.leftStick);
    const XrVector2f right = ReadStick(sActions.rightStick);
    // With smooth turning on, the right stick's sideways tilt turns the view
    // and is kept from the game, and the left stick is turned with the view so
    // that pushing it forward still moves Mario the way the player is facing.
    XrVector2f move = left;
    float rightX = right.x;
    if (SmoothTurnActive()) {
        UpdateTurn(right.x);
        rightX = 0.0f;
        const float c = SDL_cosf(sTurnYaw), s = SDL_sinf(sTurnYaw);
        move.x = left.x * c + left.y * s;
        move.y = -left.x * s + left.y * c;
    } else {
        sTurnLastTicks = 0;
    }
    // OpenXR sticks are +Y up, SDL's are +Y down.
    SDL_JoystickSetVirtualAxis(sVirtualPad, SDL_CONTROLLER_AXIS_LEFTX, StickAxis(move.x));
    SDL_JoystickSetVirtualAxis(sVirtualPad, SDL_CONTROLLER_AXIS_LEFTY, StickAxis(-move.y));
    SDL_JoystickSetVirtualAxis(sVirtualPad, SDL_CONTROLLER_AXIS_RIGHTX, StickAxis(rightX));
    SDL_JoystickSetVirtualAxis(sVirtualPad, SDL_CONTROLLER_AXIS_RIGHTY, StickAxis(-right.y));
    SDL_JoystickSetVirtualAxis(sVirtualPad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, TriggerAxis(ReadFloat(sActions.leftTrigger)));
    SDL_JoystickSetVirtualAxis(sVirtualPad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
                               TriggerAxis(ReadFloat(sActions.rightTrigger)));

    SDL_JoystickSetVirtualButton(sVirtualPad, SDL_CONTROLLER_BUTTON_A, ReadBool(sActions.a));
    SDL_JoystickSetVirtualButton(sVirtualPad, SDL_CONTROLLER_BUTTON_B, ReadBool(sActions.b));
    SDL_JoystickSetVirtualButton(sVirtualPad, SDL_CONTROLLER_BUTTON_X, ReadBool(sActions.x));
    SDL_JoystickSetVirtualButton(sVirtualPad, SDL_CONTROLLER_BUTTON_Y, ReadBool(sActions.y));
    SDL_JoystickSetVirtualButton(sVirtualPad, SDL_CONTROLLER_BUTTON_START, ReadBool(sActions.menu));
    // Clicking the left stick opens the Android launcher, leaving the game
    // paused behind it. Only with the stick at rest, so pressing down while
    // steering does not throw the player out of the game.
    const bool launcherButton = ReadBool(sActions.leftStickClick);
    const bool stickAtRest = left.x * left.x + left.y * left.y < 0.3f * 0.3f;
    if (launcherButton && !sLauncherButtonHeld && stickAtRest && !MenuVisible()) {
        OpenLauncher();
    }
    sLauncherButtonHeld = launcherButton;
    // Clicking the right stick opens and closes the engine's menu, which has no
    // other way in from a headset (the on-screen menu button is an Android
    // view). While it is open the frame is shown on the virtual screen.
    const bool menuButton = ReadBool(sActions.rightStickClick);
    if (menuButton && !sMenuButtonHeld) {
        ToggleEngineMenu();
    }
    sMenuButtonHeld = menuButton;
    SDL_JoystickSetVirtualButton(sVirtualPad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, ReadFloat(sActions.leftGrip) > 0.5f);
    // In first person with the hammer out, the right grip holds and throws it
    // instead of being the game's shoulder button.
    SDL_JoystickSetVirtualButton(sVirtualPad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                                 !HammerInPlay() && ReadFloat(sActions.rightGrip) > 0.5f);
}

// -------------------------------------------------------------- rendering --

GLuint CompileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[512] = "";
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        XR_LOGW("Shader compile failed: %s", log);
    }
    return shader;
}

void CreateCopyProgram() {
    // One oversized triangle; no vertex buffers to disturb.
    static const char* kVertex = "#version 300 es\n"
                                 "out vec2 vUv;\n"
                                 "void main() {\n"
                                 "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                 "    vUv = p;\n"
                                 "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                 "}\n";
    // The game's frame holds sRGB-encoded values in a linear target. An sRGB
    // swapchain encodes on write, so decode first and the two cancel out.
    static const char* kFragment =
        "#version 300 es\n"
        "precision highp float;\n"
        "uniform sampler2D uFrame;\n"
        "uniform int uLinearize;\n"
        "in vec2 vUv;\n"
        "out vec4 oColor;\n"
        "void main() {\n"
        "    vec3 c = texture(uFrame, vUv).rgb;\n"
        "    if (uLinearize != 0) {\n"
        "        c = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));\n"
        "    }\n"
        "    oColor = vec4(c, 1.0);\n"
        "}\n";

    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, kVertex);
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, kFragment);
    sProgram = glCreateProgram();
    glAttachShader(sProgram, vertex);
    glAttachShader(sProgram, fragment);
    glLinkProgram(sProgram);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    sLinearizeUniform = glGetUniformLocation(sProgram, "uLinearize");
    const GLint frameUniform = glGetUniformLocation(sProgram, "uFrame");

    GLint previous = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
    glUseProgram(sProgram);
    glUniform1i(frameUniform, 0);
    glUseProgram(static_cast<GLuint>(previous));

    glGenVertexArrays(1, &sVertexArray);
    glGenFramebuffers(1, &sCopyFramebuffer);
    glGenTextures(1, &sCopyTexture);
}

void DestroySwapchain(Swapchain& swapchain) {
    if (!swapchain.framebuffers.empty()) {
        glDeleteFramebuffers(static_cast<GLsizei>(swapchain.framebuffers.size()), swapchain.framebuffers.data());
    }
    if (swapchain.handle != XR_NULL_HANDLE) {
        xrDestroySwapchain(swapchain.handle);
    }
    swapchain = Swapchain();
}

bool CreateSwapchain(Swapchain& swapchain, int32_t width, int32_t height) {
    DestroySwapchain(swapchain);

    uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(sSession, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);
    if (!Check(xrEnumerateSwapchainFormats(sSession, formatCount, &formatCount, formats.data()),
               "xrEnumerateSwapchainFormats")) {
        return false;
    }
    const bool hasSrgb = std::find(formats.begin(), formats.end(), GL_SRGB8_ALPHA8) != formats.end();
    const bool hasRgba = std::find(formats.begin(), formats.end(), GL_RGBA8) != formats.end();
    if (!hasSrgb && !hasRgba) {
        XR_LOGW("Runtime offers neither SRGB8_ALPHA8 nor RGBA8");
        return false;
    }

    XrSwapchainCreateInfo info { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = hasSrgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
    info.sampleCount = 1;
    info.width = static_cast<uint32_t>(width);
    info.height = static_cast<uint32_t>(height);
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!Check(xrCreateSwapchain(sSession, &info, &swapchain.handle), "xrCreateSwapchain")) {
        swapchain.handle = XR_NULL_HANDLE;
        return false;
    }
    swapchain.width = width;
    swapchain.height = height;
    swapchain.srgb = hasSrgb;

    uint32_t imageCount = 0;
    xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr);
    swapchain.images.assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
    if (!Check(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
                                          reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain.images.data())),
               "xrEnumerateSwapchainImages")) {
        DestroySwapchain(swapchain);
        return false;
    }

    GLint previous = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
    swapchain.framebuffers.resize(imageCount);
    glGenFramebuffers(static_cast<GLsizei>(imageCount), swapchain.framebuffers.data());
    for (uint32_t i = 0; i < imageCount; i++) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, swapchain.framebuffers[i]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, swapchain.images[i].image, 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous));

    XR_LOGI("Swapchain %dx%d, %u images, %s", width, height, imageCount, hasSrgb ? "sRGB" : "linear");
    return true;
}

/**
 * Copies a finished frame, [width] x [height] in framebuffer [source], into
 * image [target] of [swapchain], scaling to fit.
 */
void CopyFrame(GLuint source, int32_t width, int32_t height, const Swapchain& swapchain, GLuint target) {
    // The renderer caches its own GL state, so everything touched here is put
    // back exactly as it was found.
    GLint readFb = 0, drawFb = 0, program = 0, vertexArray = 0, activeTexture = 0, texture = 0, sampler = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };
    GLboolean colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFb);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFb);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_SAMPLER_BINDING, &sampler);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Same size, linear to linear: valid even if the window is multisampled,
    // and leaves a texture the shader can sample.
    glBindTexture(GL_TEXTURE_2D, sCopyTexture);
    if (sCopyWidth != width || sCopyHeight != height) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sCopyFramebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sCopyTexture, 0);
        sCopyWidth = width;
        sCopyHeight = height;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sCopyFramebuffer);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target);
    glViewport(0, 0, swapchain.width, swapchain.height);
    glUseProgram(sProgram);
    glUniform1i(sLinearizeUniform, swapchain.srgb ? 1 : 0);
    glBindVertexArray(sVertexArray);
    glBindSampler(0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindSampler(0, static_cast<GLuint>(sampler));
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    glActiveTexture(static_cast<GLenum>(activeTexture));
    glBindVertexArray(static_cast<GLuint>(vertexArray));
    glUseProgram(static_cast<GLuint>(program));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFb));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFb));
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (blend) glEnable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (cull) glEnable(GL_CULL_FACE);
}

// ---------------------------------------------------------------- session --

void Shutdown(const char* why) {
    XR_LOGI("OpenXR off: %s", why);
    sPresenting = false;
    sSessionRunning = false;
    sFrameBegun = false;
    CloseVirtualPad();
    DestroySwapchain(sSwapchain);
    DestroySwapchain(sEyeSwapchains[0]);
    DestroySwapchain(sEyeSwapchains[1]);
    sHandValid = false;
    if (sRightGripSpace != XR_NULL_HANDLE) {
        xrDestroySpace(sRightGripSpace);
        sRightGripSpace = XR_NULL_HANDLE;
    }
    if (sLocalSpace != XR_NULL_HANDLE) {
        xrDestroySpace(sLocalSpace);
        sLocalSpace = XR_NULL_HANDLE;
    }
    if (sSession != XR_NULL_HANDLE) {
        xrDestroySession(sSession);
        sSession = XR_NULL_HANDLE;
    }
    if (sInstance != XR_NULL_HANDLE) {
        xrDestroyInstance(sInstance);
        sInstance = XR_NULL_HANDLE;
    }
    sActions = Actions();
    sPhase = Phase::Unavailable;
}

bool HasExtension(const std::vector<XrExtensionProperties>& available, const char* name) {
    return std::any_of(available.begin(), available.end(),
                       [name](const XrExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
}

bool CreateInstance() {
    JNIEnv* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject localActivity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env == nullptr || localActivity == nullptr) {
        return false;
    }
    JavaVM* vm = nullptr;
    env->GetJavaVM(&vm);
    // Kept for the life of the process: the runtime holds on to it.
    static jobject activity = env->NewGlobalRef(localActivity);
    env->DeleteLocalRef(localActivity);

    // The Android loader has to be told about the VM before anything else.
    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                        reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader))) ||
        initializeLoader == nullptr) {
        return false;
    }
    XrLoaderInitInfoAndroidKHR loaderInfo { XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    loaderInfo.applicationVM = vm;
    loaderInfo.applicationContext = activity;
    if (XR_FAILED(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)))) {
        return false;
    }

    // Fails here, quietly, on every device without an OpenXR runtime.
    uint32_t extensionCount = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr))) {
        return false;
    }
    std::vector<XrExtensionProperties> available(extensionCount, { XR_TYPE_EXTENSION_PROPERTIES });
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, available.data()))) {
        return false;
    }
    if (!HasExtension(available, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME) ||
        !HasExtension(available, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) {
        XR_LOGW("Runtime lacks the OpenGL ES or Android instance extension");
        return false;
    }
    const char* extensions[] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
                                 XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };

    XrInstanceCreateInfoAndroidKHR androidInfo { XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    androidInfo.applicationVM = vm;
    androidInfo.applicationActivity = activity;

    XrInstanceCreateInfo info { XR_TYPE_INSTANCE_CREATE_INFO };
    info.next = &androidInfo;
    SDL_strlcpy(info.applicationInfo.applicationName, "PaperBoat", sizeof(info.applicationInfo.applicationName));
    SDL_strlcpy(info.applicationInfo.engineName, "libultraship", sizeof(info.applicationInfo.engineName));
    info.applicationInfo.applicationVersion = 1;
    info.applicationInfo.engineVersion = 1;
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = 2;
    info.enabledExtensionNames = extensions;
    if (XR_FAILED(xrCreateInstance(&info, &sInstance))) {
        sInstance = XR_NULL_HANDLE;
        return false;
    }
    return true;
}

bool CreateSession() {
    XrSystemGetInfo systemInfo { XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!Check(xrGetSystem(sInstance, &systemInfo, &sSystem), "xrGetSystem")) {
        return false;
    }

    XrSystemProperties properties { XR_TYPE_SYSTEM_PROPERTIES };
    if (XR_SUCCEEDED(xrGetSystemProperties(sInstance, sSystem, &properties))) {
        sMaxSwapchainWidth = static_cast<int32_t>(properties.graphicsProperties.maxSwapchainImageWidth);
        sMaxSwapchainHeight = static_cast<int32_t>(properties.graphicsProperties.maxSwapchainImageHeight);
        XR_LOGI("System: %s", properties.systemName);
    }

    // Must be called before the session exists, whatever it answers.
    PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements = nullptr;
    if (!GetFunction("xrGetOpenGLESGraphicsRequirementsKHR", &getRequirements)) {
        return false;
    }
    XrGraphicsRequirementsOpenGLESKHR requirements { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR };
    if (!Check(getRequirements(sInstance, sSystem, &requirements), "xrGetOpenGLESGraphicsRequirementsKHR")) {
        return false;
    }

    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        XR_LOGW("No current EGL context");
        return false;
    }
    EGLint configId = 0;
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    eglQueryContext(display, context, EGL_CONFIG_ID, &configId);
    const EGLint configAttributes[] = { EGL_CONFIG_ID, configId, EGL_NONE };
    eglChooseConfig(display, configAttributes, &config, 1, &configCount);

    XrGraphicsBindingOpenGLESAndroidKHR binding { XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    binding.display = display;
    binding.config = config;
    binding.context = context;

    XrSessionCreateInfo sessionInfo { XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next = &binding;
    sessionInfo.systemId = sSystem;
    if (!Check(xrCreateSession(sInstance, &sessionInfo, &sSession), "xrCreateSession")) {
        sSession = XR_NULL_HANDLE;
        return false;
    }

    // LOCAL: the screen stays put while the head moves, and the headset's own
    // recentre gesture brings it back in front of the player.
    XrReferenceSpaceCreateInfo spaceInfo { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    if (!Check(xrCreateReferenceSpace(sSession, &spaceInfo, &sLocalSpace), "xrCreateReferenceSpace")) {
        return false;
    }

    uint32_t blendCount = 0;
    XrEnvironmentBlendMode blendModes[8];
    if (XR_SUCCEEDED(xrEnumerateEnvironmentBlendModes(sInstance, sSystem, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8,
                                                      &blendCount, blendModes)) &&
        blendCount > 0) {
        sBlendMode = blendModes[0];
    }

    uint32_t viewCount = 0;
    XrViewConfigurationView viewConfigs[2] = { { XR_TYPE_VIEW_CONFIGURATION_VIEW },
                                               { XR_TYPE_VIEW_CONFIGURATION_VIEW } };
    if (XR_SUCCEEDED(xrEnumerateViewConfigurationViews(sInstance, sSystem, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                       2, &viewCount, viewConfigs)) &&
        viewCount == 2) {
        sEyeWidth = std::min<int32_t>(static_cast<int32_t>(viewConfigs[0].recommendedImageRectWidth), sMaxSwapchainWidth);
        sEyeHeight =
            std::min<int32_t>(static_cast<int32_t>(viewConfigs[0].recommendedImageRectHeight), sMaxSwapchainHeight);
    }

    CreateActions();
    AttachActions();
    if (sActions.rightGripPose != XR_NULL_HANDLE) {
        XrActionSpaceCreateInfo gripInfo { XR_TYPE_ACTION_SPACE_CREATE_INFO };
        gripInfo.action = sActions.rightGripPose;
        gripInfo.poseInActionSpace.orientation.w = 1.0f;
        if (!Check(xrCreateActionSpace(sSession, &gripInfo, &sRightGripSpace), "xrCreateActionSpace")) {
            sRightGripSpace = XR_NULL_HANDLE;
        }
    }
    CreateCopyProgram();
    CreateHammer();
    return true;
}

void HandleSessionState(XrSessionState state) {
    sSessionState = state;
    switch (state) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo begin { XR_TYPE_SESSION_BEGIN_INFO };
            begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            if (Check(xrBeginSession(sSession, &begin), "xrBeginSession")) {
                // Back from the launcher, perhaps with new VR settings.
                PaperboatRecheckPendingSettings();
                sSessionRunning = true;
                OpenVirtualPad();
            }
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            sFrameBegun = false;
            sSessionRunning = false;
            sPresenting = false;
            ReleaseVirtualPad();
            xrEndSession(sSession);
            break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            Shutdown("session ended by the runtime");
            break;
        default:
            break;
    }
}

void PollEvents() {
    XrEventDataBuffer event { XR_TYPE_EVENT_DATA_BUFFER };
    while (sInstance != XR_NULL_HANDLE && xrPollEvent(sInstance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            if (changed->session == sSession) {
                HandleSessionState(changed->state);
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            Shutdown("instance lost");
        }
        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

bool BeginXrFrame() {
    sFrameState = { XR_TYPE_FRAME_STATE };
    if (!Check(xrWaitFrame(sSession, nullptr, &sFrameState), "xrWaitFrame") ||
        !Check(xrBeginFrame(sSession, nullptr), "xrBeginFrame")) {
        return false;
    }
    sFrameBegun = true;
    return true;
}

/** Fills [quad] with the flat frame from the window's framebuffer. */
bool PrepareScreenLayer(SDL_Window* window, XrCompositionLayerQuad& quad) {
    int width = 0;
    int height = 0;
    SDL_GL_GetDrawableSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        return false;
    }
    // The frame is copied one to one, so the swapchain follows the window.
    const int32_t wantedWidth = std::min<int32_t>(width, sMaxSwapchainWidth);
    const int32_t wantedHeight = std::min<int32_t>(height, sMaxSwapchainHeight);
    const bool usable = (sSwapchain.handle != XR_NULL_HANDLE && sSwapchain.width == wantedWidth &&
                         sSwapchain.height == wantedHeight) ||
                        CreateSwapchain(sSwapchain, wantedWidth, wantedHeight);

    uint32_t imageIndex = 0;
    XrSwapchainImageWaitInfo wait { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wait.timeout = XR_INFINITE_DURATION;
    if (!usable || XR_FAILED(xrAcquireSwapchainImage(sSwapchain.handle, nullptr, &imageIndex))) {
        return false;
    }
    bool ready = false;
    if (XR_SUCCEEDED(xrWaitSwapchainImage(sSwapchain.handle, &wait))) {
        CopyFrame(0, width, height, sSwapchain, sSwapchain.framebuffers[imageIndex]);

        const float distance = std::clamp(CVarGetFloat(kCVarDistance, kDefaultDistance), 0.5f, 20.0f);
        const float screenWidth = std::clamp(CVarGetFloat(kCVarWidth, kDefaultWidth), 0.5f, 40.0f);
        quad.layerFlags = 0;
        quad.space = sLocalSpace;
        quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quad.subImage.swapchain = sSwapchain.handle;
        quad.subImage.imageRect.offset = { 0, 0 };
        quad.subImage.imageRect.extent = { sSwapchain.width, sSwapchain.height };
        quad.subImage.imageArrayIndex = 0;
        quad.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
        quad.pose.position = { 0.0f, CVarGetFloat(kCVarHeight, kDefaultHeightOffset), -distance };
        quad.size = { screenWidth, screenWidth * static_cast<float>(height) / static_cast<float>(width) };
        ready = true;
    }
    xrReleaseSwapchainImage(sSwapchain.handle, nullptr);
    return ready;
}

/** Ends the frame begun by BeginXrFrame with whatever was drawn for it. */
bool EndXrFrame(SDL_Window* window) {
    XrCompositionLayerQuad quad { XR_TYPE_COMPOSITION_LAYER_QUAD };
    XrCompositionLayerProjection projection { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView projectionViews[2] = { { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW },
                                                            { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW } };
    const XrCompositionLayerBaseHeader* layers[1] = { nullptr };
    uint32_t layerCount = 0;

    if (sFrameState.shouldRender) {
        if (sEyeRendered[0] && sEyeRendered[1]) {
            for (int eye = 0; eye < 2; eye++) {
                projectionViews[eye].pose = sViews[eye].pose;
                projectionViews[eye].fov = sViews[eye].fov;
                projectionViews[eye].subImage.swapchain = sEyeSwapchains[eye].handle;
                projectionViews[eye].subImage.imageRect.offset = { 0, 0 };
                projectionViews[eye].subImage.imageRect.extent = { sEyeSwapchains[eye].width,
                                                                   sEyeSwapchains[eye].height };
                projectionViews[eye].subImage.imageArrayIndex = 0;
            }
            projection.space = sLocalSpace;
            projection.viewCount = 2;
            projection.views = projectionViews;
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection);
            layerCount = 1;
        } else if (PrepareScreenLayer(window, quad)) {
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
            layerCount = 1;
        }
    }

    XrFrameEndInfo end { XR_TYPE_FRAME_END_INFO };
    end.displayTime = sFrameState.predictedDisplayTime;
    end.environmentBlendMode = sBlendMode;
    end.layerCount = layerCount;
    end.layers = layerCount > 0 ? layers : nullptr;
    sFrameBegun = false;
    sEyeRendered[0] = sEyeRendered[1] = false;
    return Check(xrEndFrame(sSession, &end), "xrEndFrame");
}

// ----------------------------------------------------------------- hammer --

// Column-major 4x4 helpers, as GL takes them. The hammer is drawn in the
// tracking space in metres, independently of the game's own matrices.
void Mul4(const float a[16], const float b[16], float out[16]) {
    float result[16];
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            result[column * 4 + row] = a[0 * 4 + row] * b[column * 4 + 0] + a[1 * 4 + row] * b[column * 4 + 1] +
                                       a[2 * 4 + row] * b[column * 4 + 2] + a[3 * 4 + row] * b[column * 4 + 3];
        }
    }
    SDL_memcpy(out, result, sizeof(result));
}

void PoseMatrix(const XrPosef& pose, float out[16]) {
    const float x = pose.orientation.x, y = pose.orientation.y, z = pose.orientation.z, w = pose.orientation.w;
    out[0] = 1.0f - 2.0f * (y * y + z * z);
    out[1] = 2.0f * (x * y + z * w);
    out[2] = 2.0f * (x * z - y * w);
    out[3] = 0.0f;
    out[4] = 2.0f * (x * y - z * w);
    out[5] = 1.0f - 2.0f * (x * x + z * z);
    out[6] = 2.0f * (y * z + x * w);
    out[7] = 0.0f;
    out[8] = 2.0f * (x * z + y * w);
    out[9] = 2.0f * (y * z - x * w);
    out[10] = 1.0f - 2.0f * (x * x + y * y);
    out[11] = 0.0f;
    out[12] = pose.position.x;
    out[13] = pose.position.y;
    out[14] = pose.position.z;
    out[15] = 1.0f;
}

/** Inverse of a rotation-and-translation matrix. */
void InverseRigid(const float m[16], float out[16]) {
    float result[16] = { m[0], m[4], m[8], 0.0f, m[1], m[5], m[9], 0.0f, m[2], m[6], m[10], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    result[12] = -(m[0] * m[12] + m[1] * m[13] + m[2] * m[14]);
    result[13] = -(m[4] * m[12] + m[5] * m[13] + m[6] * m[14]);
    result[14] = -(m[8] * m[12] + m[9] * m[13] + m[10] * m[14]);
    SDL_memcpy(out, result, sizeof(result));
}

void EyeProjection(const XrFovf& fov, float nearClip, float farClip, float out[16]) {
    const float left = SDL_tanf(fov.angleLeft), right = SDL_tanf(fov.angleRight);
    const float up = SDL_tanf(fov.angleUp), down = SDL_tanf(fov.angleDown);
    SDL_memset(out, 0, sizeof(float) * 16);
    out[0] = 2.0f / (right - left);
    out[5] = 2.0f / (up - down);
    out[8] = (right + left) / (right - left);
    out[9] = (up + down) / (up - down);
    out[10] = -(farClip + nearClip) / (farClip - nearClip);
    out[11] = -1.0f;
    out[14] = -2.0f * farClip * nearClip / (farClip - nearClip);
}

void CreateHammer() {
    static const char* kVertex = "#version 300 es\n"
                                 "layout(location = 0) in vec3 aPosition;\n"
                                 "layout(location = 1) in vec3 aNormal;\n"
                                 "uniform mat4 uMvp;\n"
                                 "uniform mat4 uNormal;\n"
                                 "out vec3 vNormal;\n"
                                 "void main() {\n"
                                 "    vNormal = mat3(uNormal) * aNormal;\n"
                                 "    gl_Position = uMvp * vec4(aPosition, 1.0);\n"
                                 "}\n";
    static const char* kFragment = "#version 300 es\n"
                                   "precision mediump float;\n"
                                   "uniform vec3 uColor;\n"
                                   "in vec3 vNormal;\n"
                                   "out vec4 oColor;\n"
                                   "void main() {\n"
                                   "    float light = max(dot(normalize(vNormal), normalize(vec3(0.3, 0.8, 0.5))), 0.0);\n"
                                   "    oColor = vec4(uColor * (0.5 + 0.5 * light), 1.0);\n"
                                   "}\n";
    const GLuint vertex = CompileShader(GL_VERTEX_SHADER, kVertex);
    const GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, kFragment);
    sHammerProgram = glCreateProgram();
    glAttachShader(sHammerProgram, vertex);
    glAttachShader(sHammerProgram, fragment);
    glLinkProgram(sHammerProgram);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    sHammerMvpUniform = glGetUniformLocation(sHammerProgram, "uMvp");
    sHammerNormalUniform = glGetUniformLocation(sHammerProgram, "uNormal");
    sHammerColorUniform = glGetUniformLocation(sHammerProgram, "uColor");

    // A unit cube, six faces of two triangles, wound counter-clockwise from
    // outside: position then normal.
    std::vector<float> vertices;
    for (int axis = 0; axis < 3; axis++) {
        for (int side = -1; side <= 1; side += 2) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            static const float corners[6][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 } };
            for (int i = 0; i < 6; i++) {
                // The far face swaps its two in-plane axes, which mirrors it, so
                // it too is counter-clockwise seen from outside.
                float position[3] = { 0.0f, 0.0f, 0.0f };
                float normal[3] = { 0.0f, 0.0f, 0.0f };
                const float cu = corners[i][0], cv = corners[i][1];
                position[axis] = 0.5f * side;
                position[u] = 0.5f * (side > 0 ? cu : cv);
                position[v] = 0.5f * (side > 0 ? cv : cu);
                normal[axis] = static_cast<float>(side);
                vertices.insert(vertices.end(), position, position + 3);
                vertices.insert(vertices.end(), normal, normal + 3);
            }
        }
    }

    GLint previousArray = 0, previousBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousArray);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
    glGenVertexArrays(1, &sHammerVertexArray);
    glGenBuffers(1, &sHammerBuffer);
    glBindVertexArray(sHammerVertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, sHammerBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<const void*>(3 * sizeof(float)));
    glBindVertexArray(static_cast<GLuint>(previousArray));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousBuffer));
}

bool HammerShown() {
    return sFirstPerson.load(std::memory_order_relaxed) && sHandValid && CVarGetInteger(kCVarHammer, 1) != 0 &&
           PaperboatXR_HasHammer() != 0;
}

/** Whether the hammer is the player's to swing and throw right now. */
bool HammerInPlay() {
    return sFirstPerson.load(std::memory_order_relaxed) && CVarGetInteger(kCVarHammer, 1) != 0 &&
           PaperboatXR_HasHammer() != 0;
}

void EndThrow() {
    sThrowFlying = false;
    sGripWasHeld = false;
    gPaperboatXRThrowFlying = 0;
}

/**
 * Tells the game where the flying hammer is in its own world. The eye passes
 * place the world in the room by: world -> camera view -> relative to Mario's
 * eyes -> levelled -> turned -> scaled. This runs that backwards. Past the
 * anchor only rotations remain, and levelling leaves a rotation about the
 * vertical, so the camera's rotation and the levelling can be undone together.
 */
void PublishThrowPosition() {
    float view[4][4];
    PaperboatXR_GetCameraView(view);

    // Levelling, as the renderer does it: the smallest rotation taking the
    // camera's idea of world-up onto +Y (row-vector form).
    float level[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
    float up[3] = { view[1][0], view[1][1], view[1][2] };
    const float length = SDL_sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (length > 1e-6f && up[1] / length > -0.9f) {
        up[0] /= length;
        up[1] /= length;
        up[2] /= length;
        const float vx = -up[2], vz = up[0], k = 1.0f / (1.0f + up[1]);
        const float r[3][3] = { { 1.0f - k * vz * vz, -vz, k * vx * vz },
                                { vz, 1.0f - k * (vx * vx + vz * vz), -vx },
                                { k * vx * vz, vx, 1.0f - k * vx * vx } };
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                level[i][j] = r[j][i];
            }
        }
    }
    // world offset (row) * viewRotation * level = levelled offset.
    float toLevelled[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            toLevelled[i][j] = view[i][0] * level[0][j] + view[i][1] * level[1][j] + view[i][2] * level[2][j];
        }
    }

    // Room (metres) -> game units, with any smooth turn taken back out.
    float x = sThrowPosition[0], y = sThrowPosition[1], z = sThrowPosition[2];
    if (CVarGetInteger(kCVarSmoothTurn, 0) != 0) {
        x -= sTurnOffset[0];
        y -= sTurnOffset[1];
        z -= sTurnOffset[2];
        const float c = SDL_cosf(sTurnYaw), s = SDL_sinf(sTurnYaw);
        const float turnedX = c * x - s * z;
        z = s * x + c * z;
        x = turnedX;
    }
    const float levelled[3] = { x * sWorldScale, y * sWorldScale, z * sWorldScale };
    // The inverse of a rotation is its transpose.
    for (int i = 0; i < 3; i++) {
        gPaperboatXRThrowPos[i] = sAnchorNow[i] + levelled[0] * toLevelled[i][0] + levelled[1] * toLevelled[i][1] +
                                  levelled[2] * toLevelled[i][2];
    }
}

/** One frame of flight: gravity, then ask the game what it hit. */
void UpdateThrow(Uint64 now) {
    if (gPaperboatXRThrowLanded != 0 || now - sThrowStartTicks > 1500) {
        EndThrow();
        return;
    }
    const float seconds = std::min(0.05f, static_cast<float>(now - sThrowLastTicks) / 1000.0f);
    sThrowLastTicks = now;
    sThrowVelocity[1] -= 9.8f * seconds;
    for (int i = 0; i < 3; i++) {
        sThrowPosition[i] += sThrowVelocity[i] * seconds;
    }
    PublishThrowPosition();
}

/**
 * Finds the right hand for this frame and turns a swing into Mario's hammer.
 * A swing is the hammer's head moving fast: the hand's own speed plus what the
 * wrist's rotation adds at the end of the handle.
 */
void UpdateHammerHand() {
    sHandValid = false;
    if (!sFirstPerson.load(std::memory_order_relaxed) || sRightGripSpace == XR_NULL_HANDLE || !sFrameBegun) {
        EndThrow();
        return;
    }
    XrSpaceVelocity velocity { XR_TYPE_SPACE_VELOCITY };
    XrSpaceLocation location { XR_TYPE_SPACE_LOCATION };
    location.next = &velocity;
    const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if (XR_FAILED(xrLocateSpace(sRightGripSpace, sLocalSpace, sFrameState.predictedDisplayTime, &location)) ||
        (location.locationFlags & needed) != needed) {
        return;
    }
    sHandPose = location.pose;
    sHandValid = true;

    if (!HammerShown()) {
        EndThrow();
        return;
    }
    if ((velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) == 0) {
        if (sThrowFlying) {
            UpdateThrow(SDL_GetTicks64());
        }
        return;
    }
    float hand[16];
    PoseMatrix(sHandPose, hand);
    // The head sits 0.3 m along the grip's -Z, in tracking space.
    const float arm[3] = { -hand[8] * 0.3f, -hand[9] * 0.3f, -hand[10] * 0.3f };
    float speed[3] = { velocity.linearVelocity.x, velocity.linearVelocity.y, velocity.linearVelocity.z };
    if (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) {
        const XrVector3f& spin = velocity.angularVelocity;
        speed[0] += spin.y * arm[2] - spin.z * arm[1];
        speed[1] += spin.z * arm[0] - spin.x * arm[2];
        speed[2] += spin.x * arm[1] - spin.y * arm[0];
    }
    const float headSpeed = SDL_sqrtf(speed[0] * speed[0] + speed[1] * speed[1] + speed[2] * speed[2]);
    const Uint64 now = SDL_GetTicks64();

    if (sThrowFlying) {
        UpdateThrow(now);
        return;
    }

    // Grip squeezed: winding up a throw, so a fast swing is not an attack yet.
    const float grip = ReadFloat(sActions.rightGrip);
    if (grip > 0.6f) {
        sGripWasHeld = true;
        return;
    }
    if (sGripWasHeld && grip < 0.3f) {
        sGripWasHeld = false;
        // Let go while moving: the hammer leaves with the head's velocity.
        if (headSpeed > 1.5f) {
            sThrowFlying = true;
            sThrowStartTicks = sThrowLastTicks = now;
            sLastSwingTicks = now;
            sThrowOrientation = sHandPose.orientation;
            for (int i = 0; i < 3; i++) {
                sThrowVelocity[i] = speed[i];
            }
            sThrowPosition[0] = sHandPose.position.x + arm[0];
            sThrowPosition[1] = sHandPose.position.y + arm[1];
            sThrowPosition[2] = sHandPose.position.z + arm[2];
            gPaperboatXRThrowLanded = 0;
            PublishThrowPosition();
            gPaperboatXRThrowFlying = 1;
        }
        return;
    }

    if (headSpeed > 3.0f && now - sLastSwingTicks > 700) {
        sLastSwingTicks = now;
        // Held for a few game frames so the game sees a clean press.
        gPaperboatXRSwingFrames = 3;
    }
}

/** Draws the hammer over a finished eye image in the bound framebuffer. */
void DrawHammer(int eye, int32_t width, int32_t height) {
    if (!HammerShown() || sHammerProgram == 0) {
        return;
    }

    float eyeMatrix[16], view[16], projection[16], viewProjection[16], hand[16];
    PoseMatrix(sViews[eye].pose, eyeMatrix);
    InverseRigid(eyeMatrix, view);
    EyeProjection(sViews[eye].fov, 0.03f, 20.0f, projection);
    Mul4(projection, view, viewProjection);
    if (sThrowFlying) {
        // Tumbling end over end about the grip's X axis as it goes.
        const float angle = static_cast<float>(SDL_GetTicks64() - sThrowStartTicks) / 1000.0f * 14.0f;
        const float sx = SDL_sinf(angle * 0.5f), cw = SDL_cosf(angle * 0.5f);
        const XrQuaternionf& q = sThrowOrientation;
        XrPosef flying {};
        flying.orientation = { q.w * sx + q.x * cw, q.y * cw + q.z * sx, q.z * cw - q.y * sx, q.w * cw - q.x * sx };
        // Position is the head's; the boxes are laid out from the grip.
        float spun[16];
        PoseMatrix(flying, spun);
        flying.position = { sThrowPosition[0] + spun[8] * 0.3f, sThrowPosition[1] + spun[9] * 0.3f,
                            sThrowPosition[2] + spun[10] * 0.3f };
        PoseMatrix(flying, hand);
    } else {
        PoseMatrix(sHandPose, hand);
    }
    float normalMatrix[16];
    Mul4(view, hand, normalMatrix);

    // Everything touched is put back: the renderer caches its GL state.
    GLint program = 0, vertexArray = 0, depthFunc = 0, cullMode = 0, frontFace = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };
    GLboolean depthMask = GL_TRUE;
    GLboolean colorMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray);
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
    glGetIntegerv(GL_FRONT_FACE, &frontFace);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean blend = glIsEnabled(GL_BLEND);
    const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);
    const GLboolean cull = glIsEnabled(GL_CULL_FACE);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glViewport(0, 0, width, height);
    // The scene is finished and its depth is in the game's own units; the
    // hammer is the nearest thing there is, so it starts from a clean depth.
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glUseProgram(sHammerProgram);
    glBindVertexArray(sHammerVertexArray);
    glUniformMatrix4fv(sHammerNormalUniform, 1, GL_FALSE, normalMatrix);

    // Boxes in the grip's frame: the handle runs along -Z out of the top of
    // the fist, and the head lies across it along Y, the way the knuckles face.
    struct Part {
        float centre[3];
        float size[3];
        float colour[3];
    };
    static const Part kParts[] = {
        { { 0.0f, 0.0f, -0.12f }, { 0.028f, 0.028f, 0.36f }, { 0.62f, 0.40f, 0.18f } },  // handle
        { { 0.0f, 0.0f, -0.30f }, { 0.085f, 0.13f, 0.085f }, { 0.80f, 0.60f, 0.30f } },  // head
        { { 0.0f, 0.075f, -0.30f }, { 0.095f, 0.02f, 0.095f }, { 0.35f, 0.22f, 0.10f } }, // bands
        { { 0.0f, -0.075f, -0.30f }, { 0.095f, 0.02f, 0.095f }, { 0.35f, 0.22f, 0.10f } },
    };
    for (const Part& part : kParts) {
        const float local[16] = { part.size[0], 0.0f, 0.0f, 0.0f, 0.0f, part.size[1], 0.0f, 0.0f,
                                  0.0f, 0.0f, part.size[2], 0.0f, part.centre[0], part.centre[1], part.centre[2], 1.0f };
        float model[16], mvp[16];
        Mul4(hand, local, model);
        Mul4(viewProjection, model, mvp);
        glUniformMatrix4fv(sHammerMvpUniform, 1, GL_FALSE, mvp);
        glUniform3f(sHammerColorUniform, part.colour[0], part.colour[1], part.colour[2]);
        glDrawArrays(GL_TRIANGLES, 0, 36);
    }

    glBindVertexArray(static_cast<GLuint>(vertexArray));
    glUseProgram(static_cast<GLuint>(program));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
    glDepthMask(depthMask);
    glDepthFunc(static_cast<GLenum>(depthFunc));
    glCullFace(static_cast<GLenum>(cullMode));
    glFrontFace(static_cast<GLenum>(frontFace));
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (blend) glEnable(GL_BLEND);
    if (stencil) glEnable(GL_STENCIL_TEST);
    if (!depth) glDisable(GL_DEPTH_TEST);
    if (!cull) glDisable(GL_CULL_FACE);
}

// ------------------------------------------------------------ eye passes --

bool MenuVisible() {
    auto context = Ship::Context::GetRawInstance();
    auto window = context != nullptr ? context->GetWindow() : nullptr;
    auto gui = window != nullptr ? window->GetGui() : nullptr;
    return gui != nullptr && gui->GetMenuOrMenubarVisible();
}

/** How many eye passes this frame wants (0 = draw flat). */
int StereoBeginFrameImpl(Fast::StereoParams* params) {
    sEyeRendered[0] = sEyeRendered[1] = false;
    if (sPhase != Phase::Ready || !sSessionRunning) {
        return 0;
    }
    // The engine's menu is drawn over the flat frame, so show that instead.
    if (CVarGetInteger(kCVarImmersive, 1) == 0 || MenuVisible()) {
        return 0;
    }
    if (!sFrameBegun && !BeginXrFrame()) {
        return 0;
    }
    if (!sFrameState.shouldRender) {
        return 0;
    }

    XrViewLocateInfo locate { XR_TYPE_VIEW_LOCATE_INFO };
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = sFrameState.predictedDisplayTime;
    locate.space = sLocalSpace;
    XrViewState viewState { XR_TYPE_VIEW_STATE };
    uint32_t viewCount = 0;
    sViews[0] = { XR_TYPE_VIEW };
    sViews[1] = { XR_TYPE_VIEW };
    const XrViewStateFlags needed = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
    if (XR_FAILED(xrLocateViews(sSession, &locate, &viewState, 2, &viewCount, sViews)) || viewCount != 2 ||
        (viewState.viewStateFlags & needed) != needed) {
        // Tracking lost: fall back to the flat screen until it returns.
        return 0;
    }

    for (Swapchain& eye : sEyeSwapchains) {
        if (eye.handle == XR_NULL_HANDLE && !CreateSwapchain(eye, sEyeWidth, sEyeHeight)) {
            return 0;
        }
    }

    sWorldScale = std::clamp(CVarGetFloat(kCVarWorldScale, kDefaultWorldScale), 1.0f, 5000.0f);
    params->hudDistance = std::clamp(CVarGetFloat(kCVarHudDistance, kDefaultHudDistance), 0.3f, 50.0f) * sWorldScale;
    params->backdropDistance =
        std::clamp(CVarGetFloat(kCVarBackdropDistance, kDefaultBackdropDistance), 1.0f, 1000.0f) * sWorldScale;
    params->levelHorizon = CVarGetInteger(kCVarLevelHorizon, 1) != 0;
    params->nearClip = 0.0f;
    params->anchorEnabled = false;

    float eye[3];
    if (CVarGetInteger(kCVarImmersive, 1) == 2 && PaperboatXR_GetPlayerEye(eye) != 0) {
        if (gPaperboatInterpIndex == 0 || !sEyeKnown) {
            // A new game frame. A jump too big to be movement is a warp or a
            // new room: do not sweep the view across the gap.
            const float dx = eye[0] - sEyeCurrent[0], dy = eye[1] - sEyeCurrent[1], dz = eye[2] - sEyeCurrent[2];
            const bool jumped = !sEyeKnown || dx * dx + dy * dy + dz * dz > 150.0f * 150.0f;
            for (int i = 0; i < 3; i++) {
                sEyePrevious[i] = jumped ? eye[i] : sEyeCurrent[i];
                sEyeCurrent[i] = eye[i];
            }
            sEyeKnown = true;
        }
        const float step =
            std::clamp(static_cast<float>(gPaperboatInterpIndex + 1) / std::max(1, gPaperboatInterpCount), 0.0f, 1.0f);
        for (int i = 0; i < 3; i++) {
            params->anchor[i] = sEyePrevious[i] + (sEyeCurrent[i] - sEyePrevious[i]) * step;
        }
        params->anchorEnabled = true;
        SDL_memcpy(sAnchorNow, params->anchor, sizeof(sAnchorNow));
        // Looking out of Mario's head: life size, level, and able to see
        // things that are right in front of him.
        sWorldScale = std::clamp(CVarGetFloat(kCVarFirstPersonScale, kDefaultFirstPersonScale), 1.0f, 500.0f);
        params->hudDistance = std::clamp(CVarGetFloat(kCVarHudDistance, kDefaultHudDistance), 0.3f, 50.0f) * sWorldScale;
        params->backdropDistance =
            std::clamp(CVarGetFloat(kCVarBackdropDistance, kDefaultBackdropDistance), 1.0f, 1000.0f) * sWorldScale;
        params->levelHorizon = true;
        params->nearClip = 2.0f;
    } else {
        sEyeKnown = false;
    }
    return 2;
}

/** Renderer hook: the eye passes, plus the draw distance that goes with them. */
int StereoBeginFrame(Fast::StereoParams* params) {
    const int eyes = StereoBeginFrameImpl(params);
    sImmersiveLive = eyes > 0;
    sFirstPerson = eyes > 0 && params->anchorEnabled;
    UpdateHammerHand();
    // The game culls for a camera the first-person player is not looking
    // through, so there everything is drawn.
    sDrawDistance = eyes <= 0 ? 0 : sFirstPerson ? 2 : std::clamp(CVarGetInteger(kCVarDrawDistance, 0), 0, 2);
    // Far and maximum also push the far clip plane out.
    static const float kFarScale[3] = { 1.0f, 2.0f, 4.0f };
    params->farScale = kFarScale[sDrawDistance.load()];
    return eyes;
}

/**
 * Renderer hook: where one eye is, as a matrix from the game camera's view
 * space (game units) to the eye's. The tracking origin is the game camera, so
 * a head at the origin sees what the camera sees.
 */
void StereoGetEye(int eye, Fast::StereoEye* out) {
    const XrPosef& pose = sViews[eye].pose;
    const XrFovf& fov = sViews[eye].fov;
    const float x = pose.orientation.x, y = pose.orientation.y, z = pose.orientation.z, w = pose.orientation.w;
    // Eye-to-tracking-space rotation, column-vector convention.
    const float r[3][3] = {
        { 1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w) },
        { 2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w) },
        { 2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y) },
    };
    const float position[3] = { pose.position.x * sWorldScale, pose.position.y * sWorldScale,
                                pose.position.z * sWorldScale };
    // Row vectors: v_eye = v * M with M's upper block r, so that v * r is r^T v.
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out->headInv[i][j] = r[i][j];
        }
        out->headInv[i][3] = 0.0f;
    }
    for (int j = 0; j < 3; j++) {
        out->headInv[3][j] = -(position[0] * r[0][j] + position[1] * r[1][j] + position[2] * r[2][j]);
    }
    out->headInv[3][3] = 1.0f;
    // The planes (HUD, backdrop) stay in front of the player...
    SDL_memcpy(out->planeInv, out->headInv, sizeof(out->headInv));
    // ...while the world goes through the turn first: game -> tracking -> eye.
    if (CVarGetInteger(kCVarSmoothTurn, 0) != 0 && (sTurnYaw != 0.0f || sTurnOffset[0] != 0.0f || sTurnOffset[2] != 0.0f)) {
        const float c = SDL_cosf(sTurnYaw), s = SDL_sinf(sTurnYaw);
        const float turn[4][4] = {
            { c, 0.0f, -s, 0.0f },
            { 0.0f, 1.0f, 0.0f, 0.0f },
            { s, 0.0f, c, 0.0f },
            { sTurnOffset[0] * sWorldScale, sTurnOffset[1] * sWorldScale, sTurnOffset[2] * sWorldScale, 1.0f },
        };
        float combined[4][4];
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                combined[i][j] = turn[i][0] * out->headInv[0][j] + turn[i][1] * out->headInv[1][j] +
                                 turn[i][2] * out->headInv[2][j] + turn[i][3] * out->headInv[3][j];
            }
        }
        SDL_memcpy(out->headInv, combined, sizeof(combined));
    }

    out->tanLeft = SDL_tanf(fov.angleLeft);
    out->tanRight = SDL_tanf(fov.angleRight);
    out->tanUp = SDL_tanf(fov.angleUp);
    out->tanDown = SDL_tanf(fov.angleDown);
}

/** Renderer hook: an eye's image is finished in the bound draw framebuffer. */
void StereoEndEye(int eye, uint32_t width, uint32_t height) {
    Swapchain& swapchain = sEyeSwapchains[eye];
    if (!sFrameBegun || swapchain.handle == XR_NULL_HANDLE || width == 0 || height == 0) {
        return;
    }
    GLint source = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &source);
    DrawHammer(eye, static_cast<int32_t>(width), static_cast<int32_t>(height));

    uint32_t imageIndex = 0;
    XrSwapchainImageWaitInfo wait { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wait.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrAcquireSwapchainImage(swapchain.handle, nullptr, &imageIndex))) {
        return;
    }
    if (XR_SUCCEEDED(xrWaitSwapchainImage(swapchain.handle, &wait))) {
        CopyFrame(static_cast<GLuint>(source), static_cast<int32_t>(width), static_cast<int32_t>(height), swapchain,
                  swapchain.framebuffers[imageIndex]);
        sEyeRendered[eye] = true;
    }
    xrReleaseSwapchainImage(swapchain.handle, nullptr);
}

/**
 * Called on the render thread, GL context current, in place of the window
 * swap. Returns true when the frame went to the headset.
 */
bool Present(SDL_Window* window) {
    if (sPhase == Phase::Unavailable) {
        return false;
    }
    if (sPhase == Phase::Untried) {
        CVarRegisterInteger(kCVarEnabled, 1);
        CVarRegisterFloat(kCVarDistance, kDefaultDistance);
        CVarRegisterFloat(kCVarWidth, kDefaultWidth);
        CVarRegisterFloat(kCVarHeight, kDefaultHeightOffset);
        CVarRegisterInteger(kCVarImmersive, 1);
        CVarRegisterFloat(kCVarWorldScale, kDefaultWorldScale);
        CVarRegisterFloat(kCVarHudDistance, kDefaultHudDistance);
        CVarRegisterFloat(kCVarBackdropDistance, kDefaultBackdropDistance);
        CVarRegisterInteger(kCVarLevelHorizon, 1);
        CVarRegisterInteger(kCVarDrawDistance, 0);
        CVarRegisterFloat(kCVarFirstPersonScale, kDefaultFirstPersonScale);
        CVarRegisterInteger(kCVarSmoothTurn, 0);
        CVarRegisterInteger(kCVarHammer, 1);
        CVarRegisterFloat(kCVarTurnSpeed, kDefaultTurnSpeed);
        if (CVarGetInteger(kCVarEnabled, 1) == 0) {
            sPhase = Phase::Unavailable;
            return false;
        }
        if (!CreateInstance()) {
            // The ordinary case on a phone; not worth a warning.
            sPhase = Phase::Unavailable;
            XR_LOGI("No OpenXR runtime; presenting to the screen");
            return false;
        }
        if (!CreateSession()) {
            Shutdown("session could not be created");
            return false;
        }
        sPhase = Phase::Ready;
        XR_LOGI("OpenXR session created");
    }

    PollEvents();
    if (sPhase != Phase::Ready || !sSessionRunning) {
        sPresenting = false;
        return false;
    }

    PumpInput();
    if ((!sFrameBegun && !BeginXrFrame()) || !EndXrFrame(window)) {
        sFrameBegun = false;
        sPresenting = false;
        return false;
    }
    sPresenting = true;
    return true;
}

// The eye-pass hooks start out empty on the other side, like the present hook.
[[maybe_unused]] const bool sStereoInstalled =
    (Fast::gStereoHooks = { &StereoBeginFrame, &StereoGetEye, &StereoEndEye }, true);

// Constant-initialised to null on the other side, so the order the two
// translation units start up in does not matter.
[[maybe_unused]] const bool sHookInstalled = (Fast::gPresentHook = &Present, true);

} // namespace

extern "C" int32_t PaperboatXR_DrawDistance(void) {
    return sDrawDistance.load(std::memory_order_relaxed);
}

extern "C" int32_t PaperboatXR_FirstPerson(void) {
    return sFirstPerson.load(std::memory_order_relaxed) ? 1 : 0;
}

bool PaperboatXR_IsPresenting() {
    return sPresenting.load(std::memory_order_relaxed);
}

#endif // __ANDROID__
