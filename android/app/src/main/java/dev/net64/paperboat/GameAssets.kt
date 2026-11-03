package dev.net64.paperboat

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.zip.ZipInputStream

/**
 * Everything the game needs on disk, and how it gets there.
 *
 * libultraship resolves every runtime path through
 * `SDL_AndroidGetExternalStoragePath()` — `getExternalFilesDir(null)`, which a
 * file manager can also reach, so mods can be dropped in. Everything lives there:
 *
 *     Android/data/dev.net64.paperboat/files/
 *       baserom.us.z64       the user's ROM, copied in by the launcher
 *       config.yml, assets/  Torch's extraction recipes, unpacked from the APK
 *       paperboat.o2r        engine assets, built from port/ into the APK
 *       pm64.o2r             generated on-device from the ROM
 *       mods/                user mods (.o2r, .zip or plain folders)
 *       saves/               fileN.json, one per save slot
 *
 * Unpacking happens here, not in the engine: Torch needs config.yml and assets/
 * on disk before the game process starts. The zip is built by build.gradle.kts.
 */
object GameAssets {

    // Matches the engine's mobile fallback (GameExtractor.cpp), so an
    // in-game re-extraction finds it too. Torch hashes contents, not the name.
    const val ROM_NAME = "baserom.us.z64"

    private const val TAG = "GameAssets"
    private const val GAME_ARCHIVE = "pm64.o2r"
    private const val CONTROLLER_DB = "gamecontrollerdb.txt"
    private const val TORCH_HASHES = "torch.hash.yml"
    private const val GAMEDATA_ZIP = "gamedata.zip"
    private const val GAMEDATA_VERSION = "gamedata.version"
    private const val STAMP = ".gamedata.version"
    private const val FEATURES = "features"

    fun gameDir(context: Context): File =
        (context.getExternalFilesDir(null) ?: context.filesDir).also { it.mkdirs() }

    fun romFile(context: Context) = File(gameDir(context), ROM_NAME)

    /** Normalize the three standard N64 byte orders before content-hash verification. */
    fun normalizeRom(rom: File) {
        val bytes = rom.readBytes()
        require(bytes.size >= 64 && bytes.size <= 128 * 1024 * 1024) { "Invalid N64 ROM size." }
        val header = bytes.take(4).map { it.toInt() and 255 }
        when (header) {
            listOf(0x80,0x37,0x12,0x40) -> return
            listOf(0x37,0x80,0x40,0x12) -> {
                require(bytes.size % 2 == 0)
                for (i in bytes.indices step 2) { val b=bytes[i]; bytes[i]=bytes[i+1];bytes[i+1]=b }
            }
            listOf(0x40,0x12,0x37,0x80) -> {
                require(bytes.size % 4 == 0)
                for (i in bytes.indices step 4) { val a=bytes[i];val b=bytes[i+1];bytes[i]=bytes[i+3];bytes[i+1]=bytes[i+2];bytes[i+2]=b;bytes[i+3]=a }
            }
            else -> error("This file does not have a supported N64 ROM header.")
        }
        FileOutputStream(rom).use { it.write(bytes);it.fd.sync() }
    }

    fun gameArchive(context: Context) = File(gameDir(context), GAME_ARCHIVE)

    fun modsDir(context: Context) = File(gameDir(context), "mods")

    fun isExtracted(context: Context) = gameArchive(context).length() > 0

    /**
     * Unpacks the APK-bundled extraction inputs into [gameDir]. Re-runs whenever
     * the packaged zip changes, so an update ships new recipes without the user
     * clearing data. Returns an error message, or null when ready.
     */
    fun stageBundledAssets(context: Context): String? {
        val target = gameDir(context)
        val stamp = File(target, STAMP)

        return try {
            val expected = context.assets.open(GAMEDATA_VERSION).use { it.readBytes().decodeToString() }
            if (stamp.takeIf { it.isFile }?.readText() == expected) {
                return stageFeatures(context)
            }

            context.assets.open(GAMEDATA_ZIP).use { input ->
                ZipInputStream(input.buffered()).use { zip ->
                    while (true) {
                        val entry = zip.nextEntry ?: break
                        val destination = File(target, entry.name).canonicalFile

                        // An entry named ../../x would escape the game directory.
                        if (!destination.path.startsWith(target.canonicalPath + File.separator)) {
                            Log.w(TAG, "Skipping suspicious zip entry ${entry.name}")
                            zip.closeEntry()
                            continue
                        }

                        if (entry.isDirectory) {
                            destination.mkdirs()
                        } else {
                            destination.parentFile?.mkdirs()
                            FileOutputStream(destination).use { output -> zip.copyTo(output, COPY_BUFFER_BYTES) }
                        }
                        zip.closeEntry()
                    }
                }
            }

            // Users are meant to edit this one, so only seed it once.
            val controllerDb = File(target, CONTROLLER_DB)
            if (!controllerDb.isFile) {
                copyAsset(context, CONTROLLER_DB, controllerDb)
            }

            val mods = modsDir(context)
            mods.mkdirs()
            if (mods.list().isNullOrEmpty()) {
                // Only a hint; nothing reads it back, so it must not fail here.
                try {
                    copyAsset(context, "mods/place_mods_here.txt", File(mods, "place_mods_here.txt"))
                } catch (error: IOException) {
                    Log.w(TAG, "Could not write the mods placeholder", error)
                }
            }

            // New recipes make the previous archive stale, and Torch skips
            // work whose inputs it believes are unchanged.
            gameArchive(context).delete()
            File(target, TORCH_HASHES).delete()

            stamp.writeText(expected)
            stageFeatures(context)
        } catch (error: IOException) {
            Log.e(TAG, "Could not unpack the bundled assets", error)
            "Could not unpack the bundled game files: ${error.message}"
        }
    }

    fun featuresDir(context: Context) = File(gameDir(context), FEATURES)

    /**
     * Copies the built-in feature archives (Clean Text, button prompts,
     * languages) into [featuresDir], kept apart from user mods. The engine
     * mounts the ones selected in Options at start-up (Engine.cpp). Runs on
     * every launch but only rewrites files whose size changed.
     */
    fun stageFeatures(context: Context): String? = try {
        val target = featuresDir(context).also { it.mkdirs() }
        for (name in context.assets.list(FEATURES).orEmpty()) {
            if (!name.endsWith(".o2r")) continue
            val destination = File(target, name)
            val length = context.assets.openFd("$FEATURES/$name").use { it.length }
            if (destination.length() == length) continue
            val partial = File(target, "$name.tmp")
            copyAsset(context, "$FEATURES/$name", partial)
            check(partial.renameTo(destination)) { "Could not install $name" }
        }
        null
    } catch (error: Exception) {
        Log.e(TAG, "Could not install the built-in features", error)
        "Could not install the built-in features: ${error.message}"
    }

    /**
     * The name Torch knows this ROM by, or null if the recipes don't support it.
     * Answered by the engine from the same config.yml the extraction reads, so
     * the launcher carries no second list of hashes to drift out of sync.
     *
     * [stageBundledAssets] must have run first; it puts config.yml in place.
     */
    fun identifyRom(context: Context, rom: File): String? =
        nativeDetectRom(rom.absolutePath, gameDir(context).absolutePath)

    /**
     * Runs Torch over [romFile] to produce pm64.o2r. Blocking and slow — expect
     * a minute or more on older hardware. Returns an error message, or null on
     * success.
     */
    fun generateGameArchive(context: Context): String? {
        val dir = gameDir(context)
        return nativeGenerateGameArchive(romFile(context).absolutePath, dir.absolutePath, dir.absolutePath)
    }

    private const val COPY_BUFFER_BYTES = 1 shl 17

    private fun copyAsset(context: Context, assetPath: String, target: File) {
        target.parentFile?.mkdirs()
        context.assets.open(assetPath).use { input ->
            FileOutputStream(target).use { output ->
                input.copyTo(output)
                output.fd.sync()
            }
        }
    }

    private external fun nativeDetectRom(rom: String, sourceDir: String): String?
    external fun extractionProgress(): Int

    private external fun nativeGenerateGameArchive(rom: String, sourceDir: String, destDir: String): String?

    init {
        // libmain.so carries both the game and the Torch extractor.
        System.loadLibrary("SDL2")
        System.loadLibrary("main")
    }
}
