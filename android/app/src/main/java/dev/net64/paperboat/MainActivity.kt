package dev.net64.paperboat

import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.libsdl.app.SDLActivity

/**
 * The game. The on-screen controller is the engine's own, drawn inside the GL
 * surface (src/port/ui/TouchControls.cpp); the only thing layered over SDL is a
 * Mods button, shown while the engine's menu is up.
 *
 * [LauncherActivity] guarantees pm64.o2r exists before this activity starts.
 */
class MainActivity : SDLActivity() {

    private lateinit var modsButton: Button
    private lateinit var homeButton: Button
    private lateinit var portMenuButton: android.widget.ImageButton
    private lateinit var sounds: MenuSounds

    private var menuOpen = false
    private var lastInputWasTouch = false

    private val handler = Handler(Looper.getMainLooper())
    private val menuWatcher = object : Runnable {
        override fun run() {
            syncMenuState()
            val hasController=android.view.InputDevice.getDeviceIds().any { id ->
                val device=android.view.InputDevice.getDevice(id)
                device!=null&&!device.isVirtual&&((device.sources and android.view.InputDevice.SOURCE_GAMEPAD)==android.view.InputDevice.SOURCE_GAMEPAD ||
                    (device.sources and android.view.InputDevice.SOURCE_JOYSTICK)==android.view.InputDevice.SOURCE_JOYSTICK)
            }
            portMenuButton.visibility=if(hasController&&!lastInputWasTouch)android.view.View.GONE else android.view.View.VISIBLE
            when (consumeMenuSoundEvents()) {
                1 -> sounds.move()
                2, 3 -> sounds.select()
            }
            handler.postDelayed(this, MENU_POLL_MS)
        }
    }

    // org/libsdl/app stays byte-identical to the SDL release libultraship
    // pins — SDLActivity refuses to start if the two disagree on version — so
    // the library is named here rather than patched in there.
    override fun getLibraries(): Array<String> = arrayOf("SDL2", "main")

    private external fun isMenuOpen(): Boolean
    private external fun togglePortMenu()
    private external fun consumeMenuSoundEvents(): Int

    /**
     * Called by the engine from the render thread (src/port/android/OpenXR.cpp)
     * when the headset's launcher button is clicked. Unlike the Main Menu
     * button this does not finish(): the game stays alive, paused behind the
     * launcher, and Play there brings this same activity back to the front.
     */
    @Suppress("unused")
    fun openLauncherFromEngine() {
        runOnUiThread { startActivity(Intent(this, LauncherActivity::class.java)) }
    }

    private fun openPaperOptions() {
        startActivityForResult(Intent(this, PauseMenuActivity::class.java), OPTIONS_REQUEST)
    }
    override fun onBackPressed() { openPaperOptions() }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode,resultCode,data)
        if(requestCode != OPTIONS_REQUEST || resultCode != RESULT_OK) return
        when(data?.getStringExtra("action")) {
            "layout" -> {
                EngineSettings.change(this,"gTouchControls.Enabled",1f)
                EngineSettings.change(this,"gTouchControls.EditMode",1f)
            }
            "bindings", "advanced", "cheats" -> togglePortMenu()
            "import" -> {
                android.app.AlertDialog.Builder(this).setTitle("Import ROM")
                    .setMessage("Return to the main menu to replace the ROM safely.")
                    .setPositiveButton("Main Menu") { _,_->startActivity(Intent(this,LauncherActivity::class.java));finish() }
                    .setNegativeButton("Cancel",null).show()
            }
        }
    }

    override fun dispatchKeyEvent(event: android.view.KeyEvent): Boolean {
        if(event.device?.sources?.let{(it and android.view.InputDevice.SOURCE_GAMEPAD)==android.view.InputDevice.SOURCE_GAMEPAD}==true)
            lastInputWasTouch=false
        if (event.keyCode == android.view.KeyEvent.KEYCODE_BUTTON_MODE || event.keyCode == android.view.KeyEvent.KEYCODE_BUTTON_SELECT) {
            if (event.action == android.view.KeyEvent.ACTION_UP) openPaperOptions()
            return true
        }
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchTouchEvent(event: android.view.MotionEvent): Boolean {
        if(event.actionMasked==android.view.MotionEvent.ACTION_DOWN) {
            lastInputWasTouch=true
            if(::portMenuButton.isInitialized)portMenuButton.visibility=android.view.View.VISIBLE
        }
        return super.dispatchTouchEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: android.view.MotionEvent): Boolean {
        if((event.source and android.view.InputDevice.SOURCE_JOYSTICK)==android.view.InputDevice.SOURCE_JOYSTICK &&
            (kotlin.math.abs(event.getAxisValue(android.view.MotionEvent.AXIS_X))>.2f ||
             kotlin.math.abs(event.getAxisValue(android.view.MotionEvent.AXIS_Y))>.2f))lastInputWasTouch=false
        return super.dispatchGenericMotionEvent(event)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        sounds = MenuSounds(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        goFullscreen()
        addModsButton()
    }

    /**
     * Edge to edge, bars hidden. From API 35 the old fullscreen window flags are
     * ignored, so the bars go through the insets controller instead.
     * Transient-by-swipe keeps a stray swipe from resizing the window and
     * forcing the engine to rebuild its framebuffers.
     */
    private fun goFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)

        WindowInsetsControllerCompat(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        // Render into the cutout too, rather than letterboxing beside it.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        // The bars return on any focus loss; put them away again coming back.
        if (hasFocus) {
            goFullscreen()
        }
    }

    override fun onResume() {
        super.onResume()
        // Also re-sync now, so returning here never shows a stale menu state.
        syncMenuState()
        handler.removeCallbacks(menuWatcher)
        handler.postDelayed(menuWatcher, MENU_POLL_MS)
    }

    override fun onPause() {
        handler.removeCallbacks(menuWatcher)
        super.onPause()
    }

    override fun onDestroy() {
        handler.removeCallbacks(menuWatcher)
        sounds.release()
        super.onDestroy()
    }

    /**
     * The menu can be opened by its own button, a keyboard or a gamepad, and can
     * close itself, so ask the engine rather than track it here.
     */
    private fun syncMenuState() {
        val open = runCatching { isMenuOpen() }.getOrDefault(false)
        if (open == menuOpen) return

        menuOpen = open
        modsButton.visibility = if (open) android.view.View.VISIBLE else android.view.View.GONE
        homeButton.visibility = modsButton.visibility
    }

    /** Bottom-left, clear of the engine's menu toggle and of the menu itself. */
    private fun addModsButton() {
        val margin = (16 * resources.displayMetrics.density).toInt()

        modsButton = Button(this).apply {
            text = getString(R.string.mods_button)
            textSize = 12f
            isAllCaps = false
            visibility = android.view.View.GONE
            setOnClickListener { startActivity(Intent(context, ModsActivity::class.java)) }
        }

        val layout = FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.START
        ).apply { setMargins(margin, margin, margin, margin) }

        findViewById<ViewGroup>(android.R.id.content).addView(modsButton, layout)
        homeButton = Button(this).apply {
            text = "Main Menu"
            textSize = 12f
            isAllCaps = false
            visibility = android.view.View.GONE
            setOnClickListener {
                android.app.AlertDialog.Builder(this@MainActivity)
                    .setTitle("Return to main menu?")
                    .setMessage("Progress since your last save will be lost.")
                    .setNegativeButton("Cancel", null)
                    .setPositiveButton("Main Menu") { _, _ ->
                        startActivity(Intent(this@MainActivity, LauncherActivity::class.java))
                        finish()
                    }.show()
            }
        }
        val homeLayout = FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            Gravity.BOTTOM or Gravity.END
        ).apply { setMargins(margin, margin, margin, margin) }
        findViewById<ViewGroup>(android.R.id.content).addView(homeButton, homeLayout)
        portMenuButton = android.widget.ImageButton(this).apply {
            setImageResource(R.drawable.ingame_menu_button)
            contentDescription = "Menu"
            scaleType = android.widget.ImageView.ScaleType.FIT_CENTER
            setPadding(0,0,0,0)
            setBackgroundColor(android.graphics.Color.TRANSPARENT)
            isSoundEffectsEnabled = false
            setOnClickListener { sounds.select(); openPaperOptions() }
        }
        // Top-right corner, small: centred it covered dialogue boxes.
        val density = resources.displayMetrics.density
        val menuLayout = FrameLayout.LayoutParams(
            (84*density).toInt(), (36*density).toInt(),
            Gravity.TOP or Gravity.END
        ).apply { setMargins(0, (4*density).toInt(), (6*density).toInt(), 0) }
        findViewById<ViewGroup>(android.R.id.content).addView(portMenuButton, menuLayout)
    }

    private companion object {
        const val OPTIONS_REQUEST = 741
        /** Fast enough to feel immediate on a menu toggle, cheap enough to ignore. */
        const val MENU_POLL_MS = 150L
    }
}
