package dev.net64.paperboat

import android.app.AlertDialog
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import java.io.File

class PortOptionsActivity : ComponentActivity() {
    private lateinit var sounds: MenuSounds
    private lateinit var menu: PaperOptionsView
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        overridePendingTransition(0,0)
        sounds = MenuSounds(this)
        val page=intent.getStringExtra("page") ?: "home"
        val content=PaperOptionsView(this,page,sounds,::action,intent.getBooleanExtra("fromShortcut",false)).also{menu=it}
        setContentView(content)
        WindowInsetsControllerCompat(window, content).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }
    private fun action(name: String) {
        when (name) {
            "back" -> finish()
            "import" -> { setResult(RESULT_OK, Intent().putExtra("action", "import")); finish() }
            "verify" -> kotlin.concurrent.thread {
                val rom = GameAssets.romFile(this)
                val result = runCatching { if (rom.isFile) GameAssets.identifyRom(this, rom) else null }
                runOnUiThread {
                    val valid = result.getOrNull() != null
                    if (!valid) sounds.error()
                    AlertDialog.Builder(this).setTitle("Verify ROM")
                        .setMessage(if(valid) "Valid — supported USA ROM." else "No supported ROM is installed.")
                        .setPositiveButton("OK", null).show()
                }
            }
            "clear" -> AlertDialog.Builder(this).setTitle("Clear game file?")
                .setMessage("Remove the imported ROM and extracted game data? Saved games and mods are kept.")
                .setNegativeButton("Cancel", null).setPositiveButton("Clear") { _, _ ->
                    val ok = listOf(GameAssets.romFile(this), GameAssets.gameArchive(this), File(GameAssets.gameDir(this), "torch.hash.yml"))
                        .map { !it.exists() || it.delete() }.all { it }
                    if (!ok) sounds.error()
                    menu.invalidate()
                }.show()
            "saves" -> startActivity(Intent(this, SavesActivity::class.java))
            "mods" -> startActivity(Intent(this, ModsActivity::class.java))
            // Feature archives are mounted once at engine start-up (Engine.cpp),
            // so the process has to go; queued settings survive on disk.
            "restart" -> {
                startActivity(Intent(this, LauncherActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
                finishAffinity()
                Runtime.getRuntime().exit(0)
            }
            "cheats" -> if(intent.getBooleanExtra("fromShortcut",false)) {
                startActivity(Intent(this,PortOptionsActivity::class.java).putExtra("page","Cheats").putExtra("fromShortcut",true));finish()
            } else startActivity(Intent(this,PortOptionsActivity::class.java).putExtra("page","Cheats"))
            "advanced", "bindings", "layout" -> {
                setResult(RESULT_OK, Intent().putExtra("action", name)); finish()
            }
        }
    }
    override fun onDestroy() { sounds.release(); super.onDestroy() }
    override fun finish() {
        super.finish()
        overridePendingTransition(0,0)
    }
}
