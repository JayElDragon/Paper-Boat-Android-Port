package dev.net64.paperboat

import android.content.Context
import android.graphics.*
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.min

/** The supplied Cheats scene, with live controls instead of baked-in switches. */
class CheatsMenuView(context: Context, private val sounds: MenuSounds,
    private val back: () -> Unit) : View(context) {
    private val rows = SettingsCatalog.rows("Cheats")
    private val artwork = Bitmap.createScaledBitmap(BitmapFactory.decodeResource(resources, R.drawable.cheats_artwork),1672,941,true)
    private val blank = BitmapFactory.decodeResource(resources,R.drawable.cheats_blank_panel)
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val yellow = BitmapShader(Bitmap.createBitmap(artwork,835,204,180,44),Shader.TileMode.REPEAT,Shader.TileMode.REPEAT)
    private val icons = listOf(Rect(539,203,606,255),Rect(539,260,606,313),Rect(539,372,606,426),
        Rect(539,597,606,651),Rect(539,541,606,595),Rect(539,316,606,370))
    private val iconSprites=icons.map(::cutout)
    private val onSprite=cutout(Rect(1092,204,1246,253))
    private val offSprite=cutout(Rect(1092,266,1246,313))
    private val leftSprite=cutout(Rect(1022,267,1063,311))
    private val rightSprite=cutout(Rect(1268,266,1308,312))
    private var selected = 0
    private var scale = 1f
    private var left = 0f
    private var top = 0f
    private fun rowAt(x:Float,y:Float):Int = if(x in 520f..1312f && y in 194f..764f)
        ((y-194f)/95f).toInt().coerceIn(0,rows.lastIndex) else -1
    init { isFocusableInTouchMode=true;isSoundEffectsEnabled=false;requestFocus() }
    /** Discard only paper connected to a sprite's exterior, retaining enclosed whites. */
    private fun cutout(rect:Rect):Bitmap {
        val image=Bitmap.createBitmap(artwork,rect.left,rect.top,rect.width(),rect.height()).copy(Bitmap.Config.ARGB_8888,true)
        val w=image.width;val h=image.height;val pixels=IntArray(w*h);image.getPixels(pixels,0,w,0,0,w,h)
        val seen=BooleanArray(w*h);val queue=IntArray(w*h);var begin=0;var end=0
        fun add(i:Int) {
            if(seen[i])return
            seen[i]=true
            val c=pixels[i];val r=Color.red(c);val g=Color.green(c);val b=Color.blue(c)
            if((r>135&&g>125&&b>135&&maxOf(r,g,b)-minOf(r,g,b)<85)||(r>150&&g>110&&b<170&&r>b*1.15f&&g>b*1.1f)) {
                pixels[i]=Color.TRANSPARENT;queue[end++]=i
            }
        }
        for(x in 0 until w){add(x);add((h-1)*w+x)}
        for(y in 0 until h){add(y*w);add(y*w+w-1)}
        while(begin<end) {
            val i=queue[begin++];val x=i%w;val y=i/w
            if(x>0)add(i-1);if(x+1<w)add(i+1);if(y>0)add(i-w);if(y+1<h)add(i+w)
        }
        image.setPixels(pixels,0,w,0,0,w,h);return image
    }
    override fun onDraw(canvas:Canvas) {
        canvas.drawColor(Color.BLACK)
        scale=min(width/1672f,height/941f);left=(width-1672*scale)/2;top=(height-941*scale)/2
        canvas.save();canvas.translate(left,top);canvas.scale(scale,scale)
        paint.color=Color.WHITE;canvas.drawBitmap(artwork,0f,0f,paint)
        canvas.drawBitmap(blank,null,RectF(420f,165f,1340f,810f),paint)
        // Restore the taped photograph above the paper panel, following its cut edge.
        canvas.save()
        canvas.clipPath(Path().apply {
            moveTo(1347f,256f);lineTo(1438f,262f);lineTo(1446f,242f);lineTo(1552f,237f)
            lineTo(1549f,277f);lineTo(1654f,282f);lineTo(1628f,705f);lineTo(1307f,679f)
            lineTo(1340f,273f);close()
        })
        canvas.drawBitmap(artwork,0f,0f,paint);canvas.restore()
        rows.forEachIndexed { index,row ->
            val y=194f+index*95f
            if(index==selected) {
                val band=Path().apply {moveTo(512f,y+36);lineTo(530f,y+4);lineTo(1294f,y+4)
                    lineTo(1312f,y+36);lineTo(1294f,y+68);lineTo(530f,y+68);close()}
                paint.shader=yellow;canvas.drawPath(band,paint);paint.shader=null
                paint.style=Paint.Style.STROKE;paint.strokeWidth=3f;paint.color=Color.WHITE;canvas.drawPath(band,paint)
                paint.strokeWidth=1.5f;paint.color=Color.rgb(214,113,35);canvas.drawPath(band,paint);paint.style=Paint.Style.FILL
            }
            paint.color=Color.WHITE
            canvas.drawBitmap(iconSprites[index],null,RectF(542f,y+9,604f,y+63),paint)
            paint.typeface=Typeface.create("sans-serif-condensed",Typeface.BOLD);paint.textSize=35f
            val title=row.title
            if(paint.measureText(title)>420f)paint.textSize*=420f/paint.measureText(title)
            paint.style=Paint.Style.STROKE;paint.strokeWidth=5f;paint.color=Color.WHITE
            canvas.drawText(title,624f,y+49,paint)
            paint.style=Paint.Style.FILL;paint.color=Color.rgb(12,13,19);canvas.drawText(title,624f,y+49,paint)
            val enabled=EngineSettings.value(context,row.key,row.default)!=0f
            paint.color=Color.WHITE
            canvas.drawBitmap(if(enabled)onSprite else offSprite,null,RectF(1092f,y+13,1246f,y+62),paint)
            canvas.drawBitmap(leftSprite,null,RectF(1022f,y+15,1063f,y+59),paint)
            canvas.drawBitmap(rightSprite,null,RectF(1268f,y+14,1308f,y+60),paint)
            paint.color=Color.argb(140,113,94,132);paint.strokeWidth=2f
            canvas.drawLine(528f,y+80,1307f,y+80,paint)
        }
        canvas.restore()
    }
    private fun focus(index:Int) {if(index!=selected){selected=index;sounds.move();invalidate()}}
    private fun change(value:Float?=null) {
        val row=rows[selected]
        val next=value ?: if(EngineSettings.value(context,row.key,row.default)==0f)1f else 0f
        runCatching{EngineSettings.change(context,row.key,next)}.onSuccess{sounds.select()}.onFailure{sounds.error()}
        invalidate()
    }
    override fun onTouchEvent(event:MotionEvent):Boolean {
        val x=(event.x-left)/scale;val y=(event.y-top)/scale;val at=rowAt(x,y)
        if(at>=0&&(event.action==MotionEvent.ACTION_DOWN||event.action==MotionEvent.ACTION_MOVE))focus(at)
        if(event.action==MotionEvent.ACTION_UP) {
            if(at>=0){focus(at);change()}
            else if(y>840&&x<300){sounds.select();back()}
            else if(y>840&&x>1380)change()
        }
        return true
    }
    override fun onKeyDown(key:Int,event:KeyEvent):Boolean {
        when(key) {
            KeyEvent.KEYCODE_DPAD_UP->focus((selected+rows.size-1)%rows.size)
            KeyEvent.KEYCODE_DPAD_DOWN->focus((selected+1)%rows.size)
            KeyEvent.KEYCODE_DPAD_LEFT->change(0f)
            KeyEvent.KEYCODE_DPAD_RIGHT->change(1f)
            KeyEvent.KEYCODE_BUTTON_A,KeyEvent.KEYCODE_ENTER,KeyEvent.KEYCODE_DPAD_CENTER->change()
            KeyEvent.KEYCODE_BACK,KeyEvent.KEYCODE_BUTTON_B->{sounds.select();back()}
            else->return super.onKeyDown(key,event)
        }
        return true
    }
}
