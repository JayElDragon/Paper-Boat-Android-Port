package dev.net64.paperboat

import android.content.Context
import java.io.File

/** Changes are applied on the game thread and saved by the engine. */
object EngineSettings {
    init { System.loadLibrary("SDL2"); System.loadLibrary("main") }
    private external fun readValue(key: String, fallback: Float, floating: Boolean): Float
    private external fun queueChanges(payload: String)
    fun value(context: Context, key: String, default: Float, floating: Boolean = false): Float {
        val saved = context.getSharedPreferences("engine-ui", 0).getFloat(key, default)
        val pending=File(GameAssets.gameDir(context),"settings-pending.txt")
        if(pending.isFile && pending.readLines().any{it.split('|').getOrNull(1)==key})return saved
        return readValue(key, saved, floating)
    }
    fun change(context: Context, key: String, value: Float, floating: Boolean = false) {
        context.getSharedPreferences("engine-ui", 0).edit().putFloat(key, value).apply()
        val dir = GameAssets.gameDir(context)
        val target = File(dir, "settings-pending.txt")
        val existing = if (target.isFile) target.readLines().filter { it.split('|').getOrNull(1) != key } else emptyList()
        val payload = (existing + "${if (floating) "f" else "i"}|$key|$value").joinToString("\n")
        val partial = File(dir, "settings-pending.tmp")
        partial.writeText(payload)
        check(partial.renameTo(target)) { "Could not save settings" }
        queueChanges(payload)
    }
}
