# PaperBoat for Android

Unofficial Android port of [PaperBoat](https://github.com/HarbourMasters/PaperBoat)
(Harbour Masters' Paper Mario 64 port), tuned for handhelds such as the AYN Odin 3.
Made by JayElDragon. Not affiliated with Harbour Masters or Nintendo.

**No game data is included.** You need your own Paper Mario (USA) ROM
(SHA1 `3837f44cda784b466c9a2d99df70d77c322b97a0`). The app extracts it on the device.
EUR and JPN ROMs are not supported yet.

## Building

Windows: Android SDK 35 / build-tools 35.0.0, NDK 28.2.13676358, CMake 3.31.6,
JDK/JBR 17+, then from the repository root:

```powershell
./build-android.ps1            # or: cd android; ./gradlew assembleDebug
```

The APK lands in `android/app/build/outputs/apk/debug/`. arm64-v8a only.

Dependencies are vendored (no submodules), with Android fixes applied:
`external/libultraship` from JeodC/libultraship `lus-converge`
(7f1bd60e9442d83acd3bee00c9d8a07270d0777e) and `external/torch` from
HarbourMasters/Torch (7f5d55f9de53f96e61c11399cb3066f8dd8e835e).

## Built-in feature packs (optional)

Options can mount these archives at start-up. They are made by other authors and
are **not** in this repository; put your own copies in
`android/app/src/main/assets/features/` with these names before building:

| File | Pack | Option |
| - | - | - |
| `hd_ui.o2r` | HD Status/UI pack (001 Status-UI v3) | System > HD Textures |
| `hd_backgrounds.o2r` | HD backgrounds pack (003 backgrounds v3) | System > HD Textures |
| `clean_text.o2r` | PaperBoat Clean Text 4K v30 | System > Clean Text |
| `buttons_xbox.o2r`, `buttons_xbox64.o2r` | Xbox Buttons | System > Button Prompts |
| `buttons_switch.o2r` | Switch Button Prompts | System > Button Prompts |
| `lang_es.o2r` | PaperBoat Spanish Language | System > Game Language |
| `lang_fr.o2r` | Paper Mario FR | System > Game Language |

On an installed APK you can instead copy them, with the same names, to
`Android/data/dev.net64.paperboat.debug/files/features/` and restart the game.
Missing files are skipped (a warning is logged). Mount order: HD, text, buttons,
language, then the user's own `mods/` folder, so later ones win.

The CRT filter is @Lywx's CRT Shader Pack (`port/shaders/post/crt*`), adapted for
OpenGL ES. Story Book (`port/shaders/post/storybook.shader.glsl`) is an original
single-pass shader inspired by the "Paper Mario - Story Book" GShade preset.

## OpenXR (VR headsets)

On a headset with an OpenXR runtime (Quest, Pico, ...) the game is drawn in
stereo with 6DoF head tracking, and the motion controllers act as a gamepad.
Phones and handhelds have no runtime, so they present to the screen exactly as
before; the same APK serves both.

**Immersive mode (default).** The frame's display list is run once per eye. The
tracking origin is the game camera: a head at the origin sees what the camera
sees, and moving or turning it moves the view through the scene. Geometry drawn
through a perspective projection gets the eye's projection and the head pose;
HUD, text and other 2D sit on a plane where the flat screen would be, and 2D
drawn before any 3D (backdrops) on a distant one. Rectangles covering nearly the
whole screen (clears, fades) cover the whole eye.

- `gXR.Immersive` (1): 0 falls back to the virtual screen below.
- `gXR.WorldScale` (150): game units per metre. Larger = smaller diorama.
- `gXR.HudDistance` (1.8), `gXR.BackdropDistance` (40): metres.
- `gXR.DrawDistance` (0): 1 widens the game's model culling threefold and
  doubles the far clip; 2 draws every model and quadruples the far clip.
- `gXR.LevelHorizon` (1): stand the world upright instead of inheriting the
  downward tilt of the game camera.
- Recentre with the headset's own gesture; the origin is the LOCAL space.
- Known limits: the game still culls and positions screen-space effects for its
  own narrow camera, so things can pop at the edges of the original view, and
  markers the game places in screen space line up only from near the origin.
  Post-processing passes are not applied to the eye images.

All of the settings below are also on the **VR Settings** tab of the options
screens (launcher > Options, and the in-game pause menu).

**First person (experimental, `gXR.Immersive` = 2).** In the overworld the
tracking origin moves from the game camera to Mario's eyes, at life size
(`gXR.FirstPersonScale`, 25 units per metre), with Mario himself not drawn, the
horizon level, every model drawn and the near clip pulled in. Battles, menus
and the title screen use the ordinary immersive view. The game's sets are built
to be seen from the camera's side, characters are flat and face the camera, and
the stick still moves Mario relative to the game camera.

**Hand hammer (`gXR.Hammer`, on by default).** In first person, once Mario
has a hammer, one is drawn in the right hand at the controller's grip pose
(plain boxes drawn over the eye image; the game has no 3D hammer). Moving its
head faster than about 3 m/s presses B for a few game frames, so the game's own
hammer attack runs: what it hits is decided by the game, in front of Mario.
Squeezing the right grip, swinging and letting go throws it: it flies under
gravity in the room for up to 1.5 s, and each game frame `xr_game.c` tests the
stretch it covered with the hammer's own raycast (walls, blocks, switches) and
against characters. On a hit Mario's hammer action is started with its reach
moved to that spot (`PaperboatXR_HammerReach`, used in `world/action/hammer.c`
and `encounter.c`). While the hammer is out the right grip is not a shoulder
button.

**Smooth turning (`gXR.SmoothTurn`, off by default).** In either immersive
view the right stick's sideways tilt turns the world about the player's head
(`gXR.TurnSpeed`, degrees a second) instead of reaching the game as C-left and
C-right, and the left stick is rotated by the same angle so forward stays
forward. The HUD plane does not turn.

**Virtual screen.** The flat frame on a quad in front of the player. Used when
immersive is off, while the engine's menu is open, and if tracking is lost.

- Screen placement, in metres, via CVars in `paperboat.cfg.json`:
  `gXR.ScreenDistance` (2.5), `gXR.ScreenWidth` (3.6), `gXR.ScreenHeight` (0,
  offset from eye level). `gXR.Enabled` = 0 turns OpenXR off.
- Touch controllers: left stick = stick, right stick = right stick (C buttons),
  A/B/X/Y = A/B/X/Y, triggers = triggers, grips = shoulders, left menu = Start.
  Clicking the right stick opens and closes the engine menu; while it is open
  the game is shown on the virtual screen so the menu is visible, and the
  left stick and A/B navigate it. Clicking the left stick (at rest) opens the
  launcher without ending the game; Play there returns to it.
  They arrive as an SDL virtual controller, so the usual rebinding applies.
- The Android-view overlays (Mods, Main Menu, the illustrated pause menu) are
  2D panels and are not drawn on the virtual screen.

The loader is `org.khronos.openxr:openxr_loader_for_android` (Gradle, via
prefab). Code: `src/port/android/OpenXR.cpp`, hooked in through
`Fast::gPresentHook` in libultraship's `gfx_sdl2.cpp` and `Fast::gStereoHooks`
(`fast/stereo.h`, the `Stereo*` functions in `interpreter.cpp`).

## Android-specific code

- `android/app/src/main/java/dev/net64/paperboat/` - launcher, illustrated menus,
  options (`SettingsCatalog`, `PaperOptionsView`), pause menu, feature staging.
- `src/port/Engine.cpp` - `MountBuiltInFeatures`, `UpdateBuiltInPostPasses`.
- `src/port/android/AndroidBridge.cpp` - settings bridge between Kotlin and CVars.
- `src/port/ui/TouchControls.cpp` - on-screen controls and layout editor.
