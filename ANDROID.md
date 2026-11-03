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

## Android-specific code

- `android/app/src/main/java/dev/net64/paperboat/` - launcher, illustrated menus,
  options (`SettingsCatalog`, `PaperOptionsView`), pause menu, feature staging.
- `src/port/Engine.cpp` - `MountBuiltInFeatures`, `UpdateBuiltInPostPasses`.
- `src/port/android/AndroidBridge.cpp` - settings bridge between Kotlin and CVars.
- `src/port/ui/TouchControls.cpp` - on-screen controls and layout editor.
