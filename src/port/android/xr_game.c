/**
 * What the OpenXR layer (OpenXR.cpp) needs to know about the game itself.
 */
#ifdef __ANDROID__

#include "common.h"
#include "game_modes.h"
#include "npc.h"

/**
 * Where Mario's eyes are, in world units, for the first-person view. Returns 0
 * when there is no overworld Mario to stand in (battles, menus, the title
 * screen), and the caller falls back to the ordinary camera.
 */
s32 PaperboatXR_GetPlayerEye(f32* out) {
    PlayerStatus* playerStatus = &gPlayerStatus;

    if (get_game_mode() != GAME_MODE_WORLD) {
        return 0;
    }
    out[0] = playerStatus->pos.x;
    out[1] = playerStatus->pos.y + playerStatus->colliderHeight * 0.85f;
    out[2] = playerStatus->pos.z;
    return 1;
}

/** Whether Mario has a hammer to swing yet. */
s32 PaperboatXR_HasHammer(void) {
    return get_game_mode() == GAME_MODE_WORLD && gPlayerData.hammerLevel >= 0;
}

/**
 * Game frames left to hold the hammer button for a swing made with the
 * controller. Written from the render thread, read once a game frame here.
 */
volatile s32 gPaperboatXRSwingFrames = 0;

/**
 * The game camera's view matrix (world to view, row vectors), with the lead and
 * shake the game folds into its projection. The OpenXR side needs it to turn a
 * point in the player's room back into a point in the world.
 */
void PaperboatXR_GetCameraView(f32 out[4][4]) {
    Camera* camera = &gCameras[CAM_DEFAULT];
    s32 i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            out[i][j] = camera->mtxViewPlayer[i][j];
        }
    }
    if (camera->flags & CAMERA_FLAG_LEAD_PLAYER) {
        guMtxCatF(out, camera->mtxViewLeading, out);
    }
    if (camera->flags & CAMERA_FLAG_SHAKING) {
        guMtxCatF(out, camera->mtxViewShaking, out);
    }
}

// A thrown hammer. The OpenXR side flies it and reports where it is in the
// world; each game frame the stretch it covered is tested here against what a
// hammer can hit. On a hit, Mario's own hammer action is started with its
// reach moved out to that spot, so blocks, switches and enemies react as they
// would to a swing made standing next to them.
volatile s32 gPaperboatXRThrowFlying = 0;
volatile f32 gPaperboatXRThrowPos[3] = { 0.0f, 0.0f, 0.0f };
volatile s32 gPaperboatXRThrowLanded = 0;

static s32 sThrowHasPrevious = false;
static f32 sThrowPrevious[3];
static s32 sReachFrames = 0;
static f32 sReachOrigin[3];
static f32 sReachPoint[3];
static f32 sReachYaw;

static void xr_throw_land(f32 x, f32 y, f32 z, f32 yaw, b32 strike) {
    gPaperboatXRThrowLanded = true;
    sThrowHasPrevious = false;
    if (strike) {
        f32 sinTheta, cosTheta;

        sin_cos_rad(DEG_TO_RAD(yaw), &sinTheta, &cosTheta);
        // The hammer action probes a line of points ahead of its origin, so
        // start a little short of the spot.
        sReachOrigin[0] = x - sinTheta * 6.0f;
        sReachOrigin[1] = y;
        sReachOrigin[2] = z + cosTheta * 6.0f;
        sReachPoint[0] = x;
        sReachPoint[1] = y;
        sReachPoint[2] = z;
        sReachYaw = yaw;
        // Long enough for the press to register and the swing to play out.
        sReachFrames = 24;
        gPaperboatXRSwingFrames = 3;
    }
}

static void xr_update_throw(void) {
    PlayerStatus* playerStatus = &gPlayerStatus;
    f32 current[3];
    f32 dx, dz, length, yaw;
    f32 x, y, z;
    s32 i;

    if (sReachFrames > 0) {
        sReachFrames--;
    }
    if (!gPaperboatXRThrowFlying || gPaperboatXRThrowLanded || get_game_mode() != GAME_MODE_WORLD) {
        sThrowHasPrevious = false;
        return;
    }
    current[0] = gPaperboatXRThrowPos[0];
    current[1] = gPaperboatXRThrowPos[1];
    current[2] = gPaperboatXRThrowPos[2];
    if (!sThrowHasPrevious) {
        sThrowPrevious[0] = current[0];
        sThrowPrevious[1] = current[1];
        sThrowPrevious[2] = current[2];
        sThrowHasPrevious = true;
        return;
    }

    dx = current[0] - sThrowPrevious[0];
    dz = current[2] - sThrowPrevious[2];
    length = sqrtf(SQ(dx) + SQ(dz));
    yaw = (length > 0.01f) ? atan2(sThrowPrevious[0], sThrowPrevious[2], current[0], current[2])
                           : playerStatus->targetYaw;

    // Walls and entities (blocks, switches, trees): the hammer's own raycast,
    // lowered so that the ray it casts above its origin passes through the
    // hammer.
    if (length > 0.01f) {
        x = sThrowPrevious[0];
        y = sThrowPrevious[1] - playerStatus->colliderHeight * 0.286f;
        z = sThrowPrevious[2];
        if (player_test_lateral_overlap(PLAYER_COLLISION_HAMMER, playerStatus, &x, &y, &z, length, yaw) > NO_COLLIDER) {
            xr_throw_land(current[0], y, current[2], yaw, true);
            return;
        }
    }

    // Characters are not part of that raycast.
    for (i = 0; i < MAX_NPCS; i++) {
        Npc* npc = get_npc_by_index(i);
        f32 reach;

        if (npc == nullptr || npc->flags == 0 || (npc->flags & (NPC_FLAG_INACTIVE | NPC_FLAG_PARTNER))) {
            continue;
        }
        reach = npc->collisionDiameter * 0.5f + 8.0f;
        if (SQ(npc->pos.x - current[0]) + SQ(npc->pos.z - current[2]) < SQ(reach)
            && current[1] > npc->pos.y - 8.0f && current[1] < npc->pos.y + npc->collisionHeight + 8.0f
        ) {
            xr_throw_land(npc->pos.x, npc->pos.y, npc->pos.z, yaw, true);
            return;
        }
    }

    // Fallen well below Mario's feet: it has hit the ground somewhere.
    if (current[1] < playerStatus->pos.y - 20.0f) {
        xr_throw_land(current[0], current[1], current[2], yaw, false);
        return;
    }

    sThrowPrevious[0] = current[0];
    sThrowPrevious[1] = current[1];
    sThrowPrevious[2] = current[2];
}

/** While a thrown hammer's strike is playing out: where the swing reaches from. */
s32 PaperboatXR_HammerReach(f32* x, f32* y, f32* z, f32* yaw) {
    if (sReachFrames <= 0) {
        return 0;
    }
    *x = sReachOrigin[0];
    *y = sReachOrigin[1];
    *z = sReachOrigin[2];
    *yaw = sReachYaw;
    return 1;
}

/** ...and the spot it struck. */
s32 PaperboatXR_HammerReachPoint(f32* x, f32* y, f32* z) {
    if (sReachFrames <= 0) {
        return 0;
    }
    *x = sReachPoint[0];
    *y = sReachPoint[1];
    *z = sReachPoint[2];
    return 1;
}

// Called once a game frame, from update_input.
s32 PaperboatXR_InjectedButtons(void) {
    xr_update_throw();
    if (gPaperboatXRSwingFrames > 0) {
        gPaperboatXRSwingFrames--;
        return BUTTON_B;
    }
    return 0;
}

#endif // __ANDROID__
