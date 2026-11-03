package dev.net64.paperboat

import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.ContextThemeWrapper
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class MenuUiTest {
    @Test fun renderAndExerciseMenuControls() {
        val instrumentation=InstrumentationRegistry.getInstrumentation()
        var failure:Throwable?=null
        instrumentation.runOnMainSync {
            try {
                val context=ContextThemeWrapper(instrumentation.targetContext,android.R.style.Theme_Material_NoActionBar)
                val directory=File(GameAssets.gameDir(context),"ui-verification").apply{mkdirs()}
                val sounds=MenuSounds(context)
                fun render(view:View,name:String) {
                    view.measure(View.MeasureSpec.makeMeasureSpec(1920,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(1080,View.MeasureSpec.EXACTLY))
                    view.layout(0,0,1920,1080)
                    val image=Bitmap.createBitmap(1920,1080,Bitmap.Config.ARGB_8888)
                    view.draw(Canvas(image))
                    File(directory,"$name.png").outputStream().use{image.compress(Bitmap.CompressFormat.PNG,100,it)}
                    image.recycle()
                }
                val menu=ArtworkMenu(context,sounds){ }
                render(menu,"main-initial")
                for(index in 0..4) {
                    val button=menu.getChildAt(index+1) as Button
                    val touch=MotionEvent.obtain(0,0,MotionEvent.ACTION_DOWN,10f,10f,0)
                    button.dispatchTouchEvent(touch);touch.recycle()
                    val cancel=MotionEvent.obtain(0,10,MotionEvent.ACTION_CANCEL,10f,10f,0)
                    button.dispatchTouchEvent(cancel);cancel.recycle()
                    render(menu,"main-$index")
                }
                val cheats=CheatsMenuView(context,sounds){ }
                render(cheats,"cheats")
                val key="gCheats.InfiniteHealth"
                val original=EngineSettings.value(context,key,0f)
                val down=MotionEvent.obtain(0,0,MotionEvent.ACTION_DOWN,1325f,265f,0)
                val up=MotionEvent.obtain(0,20,MotionEvent.ACTION_UP,1325f,265f,0)
                cheats.onTouchEvent(down);cheats.onTouchEvent(up);down.recycle();up.recycle()
                assertEquals("Touch must toggle the actual HP setting",if(original==0f)1f else 0f,EngineSettings.value(context,key,0f))
                render(cheats,"cheats-touch-toggle")
                cheats.onKeyDown(KeyEvent.KEYCODE_DPAD_RIGHT,KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_DPAD_RIGHT))
                assertEquals("Controller Right enables the HP setting",1f,EngineSettings.value(context,key,0f))
                cheats.onKeyDown(KeyEvent.KEYCODE_DPAD_LEFT,KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_DPAD_LEFT))
                assertEquals("Controller Left disables the HP setting",0f,EngineSettings.value(context,key,0f))
                EngineSettings.change(context,key,original)
                listOf("home","Graphics","StatusBar","Audio","Controls","System","Cheats","files").forEach { page ->
                    render(PaperOptionsView(context,page,sounds,{}),page.lowercase())
                }
                // Every Options home selection, for the outline and pigment checks.
                val optionsHome=PaperOptionsView(context,"home",sounds,{})
                for(index in 0..3) {
                    render(optionsHome,"home-$index")
                    optionsHome.onKeyDown(KeyEvent.KEYCODE_DPAD_DOWN,KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_DPAD_DOWN))
                }
                var shortcutBack=false
                val inGame=PaperOptionsView(context,"Graphics",sounds,{if(it=="back")shortcutBack=true},true)
                inGame.onKeyDown(KeyEvent.KEYCODE_BUTTON_B,KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_B))
                assertTrue("In-game Back must return directly to the shortcut activity",shortcutBack)
                var launcherBack=false
                val fromLauncher=PaperOptionsView(context,"Graphics",sounds,{if(it=="back")launcherBack=true})
                fromLauncher.onKeyDown(KeyEvent.KEYCODE_BUTTON_B,KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_B))
                assertFalse("The launcher keeps its Options category screen",launcherBack)
                var externalCheats=false
                val tabs=PaperOptionsView(context,"System",sounds,{externalCheats=true})
                tabs.onKeyDown(KeyEvent.KEYCODE_BUTTON_R1,KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_R1))
                assertFalse("Cheats must stay in the same tab container",externalCheats)
                render(tabs,"cheats-inline")
                val hpBefore=EngineSettings.value(context,key,0f)
                tabs.onKeyDown(KeyEvent.KEYCODE_BUTTON_A,KeyEvent(KeyEvent.ACTION_DOWN,KeyEvent.KEYCODE_BUTTON_A))
                assertEquals("The inline Cheats tab controls the real setting",if(hpBefore==0f)1f else 0f,EngineSettings.value(context,key,0f))
                EngineSettings.change(context,key,hpBefore)
                val pause=PauseView(context,sounds){}
                val pauseCenters=listOf(785f to 345f,1190f to 345f,785f to 470f,1190f to 470f,785f to 590f,1190f to 590f)
                render(pause,"pause-initial")
                pauseCenters.forEachIndexed { index,point ->
                    val event=MotionEvent.obtain(0,0,MotionEvent.ACTION_DOWN,point.first*1920/1672,point.second*1080/941,0)
                    pause.onTouchEvent(event);event.recycle();render(pause,"pause-$index")
                }
                sounds.release()
            } catch(error:Throwable) { failure=error }
        }
        failure?.let{throw it}
    }
}
