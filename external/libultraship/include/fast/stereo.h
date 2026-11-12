#pragma once

#include <stdint.h>

// Stereo (head-mounted display) rendering.
//
// When the application installs these hooks and beginFrame() asks for eye
// passes, Interpreter::Run executes the frame's display list once per eye. In
// each pass, geometry drawn through a perspective projection has that
// projection replaced by the eye's own, with the head pose inserted in the
// game's view space, so the world is seen in stereo from wherever the head is.
// Everything else (HUD, text, 2D backdrops) is placed on a plane in that same
// space, where the game's screen would be.
//
// All matrices use the interpreter's row-vector convention (clip = v * M).

namespace Fast {

struct StereoEye {
    // Game view space -> this eye's view space, in game units.
    float headInv[4][4];
    // The same for the HUD and backdrop planes. It differs when the
    // application turns the world about the player: the planes stay put.
    float planeInv[4][4];
    // Tangents of the eye's field-of-view half angles (left and down negative).
    float tanLeft, tanRight, tanUp, tanDown;
};

struct StereoParams {
    // Distance from the game camera to the HUD plane, in game units.
    float hudDistance;
    // Distance to the plane for 2D drawn before any 3D geometry (backdrops).
    float backdropDistance;
    // Rotate the world so the game's "up" is the player's, rather than
    // inheriting the tilt of a camera that looks down on the scene.
    bool levelHorizon;
    // Multiplier on the game's far clip distance for the world (1 = as is).
    float farScale;
    // Near clip distance for the world in game units, or 0 to keep the game's.
    float nearClip;
    // First person: put the tracking origin at this world position instead of
    // at the game camera. Only applied under the game's world camera.
    bool anchorEnabled;
    float anchor[3];
};

struct StereoHooks {
    // Start of a frame. Returns the number of eye passes; 0 renders normally.
    int (*beginFrame)(StereoParams* params);
    // Parameters for one eye pass.
    void (*getEye)(int eye, StereoEye* out);
    // One eye is finished and its image is in the bound draw framebuffer.
    void (*endEye)(int eye, uint32_t width, uint32_t height);
};

extern StereoHooks gStereoHooks;

} // namespace Fast
