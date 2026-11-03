package dev.net64.paperboat

import android.content.Intent
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Reproduce the renderer fault with the already imported, user-owned game archive. */
@RunWith(AndroidJUnit4::class)
class TextureRenderDiagnosticTest {
    @Test fun captureNativeTextureLoads() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        val context=instrumentation.targetContext
        check(GameAssets.stageBundledAssets(context)==null) { "Could not stage the current APK resources" }
        File(GameAssets.gameDir(context),"launcher-menu.txt").writeText("")
        val monitor=instrumentation.addMonitor(MainActivity::class.java.name,null,false)
        context.startActivity(Intent(context,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        val activity=instrumentation.waitForMonitorWithTimeout(monitor,10000)
            ?: error("Game activity failed to start")
        instrumentation.runOnMainSync {
            activity.setTurnScreenOn(true)
            activity.setShowWhenLocked(true)
            activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
        SystemClock.sleep(32000)
        val directory=File(GameAssets.gameDir(context),"ui-verification").apply{mkdirs()}
        val bitmap=instrumentation.uiAutomation.takeScreenshot() ?: error("Could not capture the game screen")
        try {
            File(directory,"texture-native.png").outputStream().use{bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG,100,it)}
            val colors=mutableSetOf<Int>()
            for(y in 0 until bitmap.height step 37)for(x in 0 until bitmap.width step 37) {
                val color=bitmap.getPixel(x,y)
                colors.add(color and 0x00F0F0F0)
            }
            check(colors.size>16) { "The game did not produce a useful frame; a blank capture is not a render pass" }
        } finally {
            bitmap.recycle()
        }
        instrumentation.removeMonitor(monitor)
    }
}
