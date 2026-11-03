package dev.net64.paperboat

import android.content.Context
import android.media.AudioAttributes
import android.media.SoundPool

class MenuSounds(context: Context) {
    private val pool = SoundPool.Builder().setMaxStreams(3).setAudioAttributes(
        AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build()
    ).build()
    private val loaded = java.util.concurrent.ConcurrentHashMap.newKeySet<Int>()
    private val navigate: Int
    private val confirm: Int
    private val installed: Int
    private val error: Int
    init {
        pool.setOnLoadCompleteListener { _, id, status -> if (status == 0) loaded.add(id) }
        navigate = pool.load(context, R.raw.choice_right, 1)
        confirm = pool.load(context, R.raw.coin_collect, 1)
        installed = pool.load(context, R.raw.item_storage, 1)
        error = pool.load(context, R.raw.choice_wrong, 1)
    }
    fun move() = play(navigate)
    fun select() = play(confirm)
    fun installed() = play(installed)
    fun error() = play(error)
    private fun play(id: Int) { if (loaded.contains(id)) pool.play(id, 0.7f, 0.7f, 1, 0, 1f) }
    fun release() = pool.release()
}
