#pragma once

/**
 * True while frames are going to an OpenXR headset rather than the screen.
 * Always false off Android and on devices with no OpenXR runtime.
 */
#ifdef __ANDROID__
bool PaperboatXR_IsPresenting();
#else
inline bool PaperboatXR_IsPresenting() {
    return false;
}
#endif
