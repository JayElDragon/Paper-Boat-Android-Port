package dev.net64.paperboat

import android.content.Intent
import android.graphics.*
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import kotlin.math.min

/** A translucent activity pauses SDL while retaining the real game frame behind it. */
class PauseMenuActivity:ComponentActivity() {
    private lateinit var sounds:MenuSounds
    private val settings=registerForActivityResult(ActivityResultContracts.StartActivityForResult()){result->
        if(result.resultCode==RESULT_OK){setResult(RESULT_OK,result.data);finish()}
    }
    override fun onCreate(state:Bundle?){
        super.onCreate(state);overridePendingTransition(0,0)
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        sounds=MenuSounds(this)
        val view=PauseView(this,sounds){page->
            if(page=="Resume")finish()
            else settings.launch(Intent(this,PortOptionsActivity::class.java).putExtra("page",page).putExtra("fromShortcut",true))
        }
        setContentView(view)
        WindowInsetsControllerCompat(window,view).apply{hide(WindowInsetsCompat.Type.systemBars());systemBarsBehavior=WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE}
    }
    override fun finish(){super.finish();overridePendingTransition(0,0)}
    override fun onDestroy(){sounds.release();super.onDestroy()}
}

class PauseView(context:android.content.Context,private val sounds:MenuSounds,private val choose:(String)->Unit):View(context){
    private val names=listOf("Graphics","Audio","Controls","System","Cheats","Resume")
    private val areas=listOf(Rect(583,285,990,408),Rect(998,291,1383,405),Rect(582,411,990,527),Rect(998,411,1386,528),Rect(582,531,988,649),Rect(997,531,1386,650))
    // pause_panel has all six buttons as plain paper: Graphics was rebuilt from
    // Controls' shape, with the old edge and rays patches baked in.
    private val source=Bitmap.createScaledBitmap(BitmapFactory.decodeResource(resources,R.drawable.pause_panel),1672,941,true)
        .copy(Bitmap.Config.ARGB_8888,true)
    private val scenes by lazy{(0..5).map{index->PaperSkin.selected(source,areas,index,-1).also{bitmap->
        MenuArtwork.outline(bitmap,source,areas[index])
        drawAttention(Canvas(bitmap),areas[index])
    }}}
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private var selected=0;private var scale=1f;private var left=0f;private var top=0f
    init{isFocusableInTouchMode=true;isSoundEffectsEnabled=false;requestFocus()}
    private fun drawAttention(canvas:Canvas,area:Rect) {
        val x=area.right-30f;val y=area.top-10f
        val rays=listOf(floatArrayOf(1f,1f,8f,3f,5f,15f,1f,13f),
            floatArrayOf(16f,3f,21f,9f,12f,18f,8f,14f),
            floatArrayOf(28f,16f,29f,23f,16f,25f,15f,20f))
        val pen=Paint(Paint.ANTI_ALIAS_FLAG)
        rays.forEach { points ->
            val path=Path().apply{moveTo(x+points[0],y+points[1]);for(i in 2 until points.size step 2)lineTo(x+points[i],y+points[i+1]);close()}
            pen.style=Paint.Style.STROKE;pen.strokeJoin=Paint.Join.ROUND;pen.strokeWidth=4f;pen.color=Color.WHITE;canvas.drawPath(path,pen)
            pen.style=Paint.Style.FILL;pen.color=Color.rgb(224,34,22);canvas.drawPath(path,pen)
        }
    }
    override fun onDraw(c:Canvas){scale=min(width/1672f,height/941f);left=(width-1672*scale)/2;top=(height-941*scale)/2;c.save();c.translate(left,top);c.scale(scale,scale);c.drawBitmap(scenes[selected],0f,0f,paint);c.restore()}
    private fun focus(i:Int){if(i!=selected){selected=i;sounds.move();invalidate()}}
    private fun confirm(){sounds.select();choose(names[selected])}
    override fun onTouchEvent(event:MotionEvent):Boolean{
        val x=((event.x-left)/scale).toInt();val y=((event.y-top)/scale).toInt();val at=areas.indexOfFirst{it.contains(x,y)}
        if(at>=0&&(event.action==MotionEvent.ACTION_DOWN||event.action==MotionEvent.ACTION_MOVE))focus(at)
        if(event.action==MotionEvent.ACTION_UP){if(at>=0)confirm()else if(y>790&&x<600)choose("Resume")}
        return true
    }
    override fun onKeyDown(key:Int,event:KeyEvent):Boolean{
        when(key){
            KeyEvent.KEYCODE_DPAD_UP->focus((selected+4)%6)
            KeyEvent.KEYCODE_DPAD_DOWN->focus((selected+2)%6)
            KeyEvent.KEYCODE_DPAD_LEFT,KeyEvent.KEYCODE_DPAD_RIGHT->focus(selected xor 1)
            KeyEvent.KEYCODE_BUTTON_A,KeyEvent.KEYCODE_ENTER,KeyEvent.KEYCODE_DPAD_CENTER->confirm()
            KeyEvent.KEYCODE_BACK,KeyEvent.KEYCODE_BUTTON_B,KeyEvent.KEYCODE_BUTTON_MODE->choose("Resume")
            else->return super.onKeyDown(key,event)
        };return true
    }
}
