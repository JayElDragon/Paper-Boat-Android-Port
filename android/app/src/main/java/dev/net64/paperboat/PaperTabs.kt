package dev.net64.paperboat

import android.content.res.Resources
import android.graphics.*

/** Uniform paper tabs, with the selected tab raised and drawn last. */
class PaperTabs(resources:Resources) {
    private val names=listOf("Graphics","Audio","Controls","System","Cheats","VR Settings")
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val base=BitmapFactory.decodeResource(resources,R.drawable.settings_blank)
    private val paper=BitmapShader(Bitmap.createBitmap(base,440,460,400,200),Shader.TileMode.REPEAT,Shader.TileMode.REPEAT)
    private val art=BitmapFactory.decodeResource(resources,R.drawable.options_panel)
    private val icons=listOf(Rect(119,69,158,110),Rect(285,74,321,107),Rect(426,73,470,108),Rect(579,69,620,111)).map(::sprite)
    private fun sprite(rect:Rect):Bitmap {
        val image=Bitmap.createBitmap(art,rect.left,rect.top,rect.width(),rect.height()).copy(Bitmap.Config.ARGB_8888,true)
        val w=image.width;val h=image.height;val p=IntArray(w*h);image.getPixels(p,0,w,0,0,w,h)
        val seen=BooleanArray(p.size);val queue=IntArray(p.size);var begin=0;var end=0
        fun add(i:Int){if(seen[i])return;seen[i]=true;val c=p[i];val r=Color.red(c);val g=Color.green(c);val b=Color.blue(c)
            if((r>125&&g>115&&b>125&&maxOf(r,g,b)-minOf(r,g,b)<85)||(r>150&&g>110&&b<170&&r>b*1.15f&&g>b*1.1f)){p[i]=0;queue[end++]=i}}
        for(x in 0 until w){add(x);add((h-1)*w+x)};for(y in 0 until h){add(y*w);add(y*w+w-1)}
        while(begin<end){val i=queue[begin++];val x=i%w;val y=i/w;if(x>0)add(i-1);if(x+1<w)add(i+1);if(y>0)add(i-w);if(y+1<h)add(i+w)}
        image.setPixels(p,0,w,0,0,w,h);return image
    }
    fun draw(c:Canvas,active:Int) {
        fun tab(index:Int) {
            val l=LEFT+index*STEP;val r=l+STEP-2;val t=if(index==active)60f else 73f
            val path=Path().apply{moveTo(l,119f);lineTo(l+2,t+15);quadTo(l+3,t,l+16,t);lineTo(r-16,t);quadTo(r-3,t,r-2,t+15);lineTo(r,119f);close()}
            c.save();c.translate(0f,2f);paint.color=Color.rgb(30,18,49);c.drawPath(path,paint);c.restore()
            paint.shader=paper;paint.colorFilter=if(index==active)PorterDuffColorFilter(Color.rgb(255,220,39),PorterDuff.Mode.MULTIPLY)else null
            c.drawPath(path,paint);paint.shader=null;paint.colorFilter=null
            paint.style=Paint.Style.STROKE;paint.color=Color.rgb(22,18,30);paint.strokeWidth=2.5f;c.drawPath(path,paint)
            c.save();c.clipPath(path);paint.color=Color.WHITE;paint.strokeWidth=1.3f;c.translate(0f,1.5f);c.drawPath(path,paint);c.restore()
            if(index==active){paint.color=Color.rgb(222,34,24);paint.strokeWidth=1.3f;c.save();c.clipPath(path);c.scale(.966f,.91f,(l+r)/2,(t+119)/2);c.drawPath(path,paint);c.restore()}
            paint.style=Paint.Style.FILL;paint.color=Color.WHITE;val cy=(t+119)/2+1
            if(index<4||index==5)drawIcon(c,index,l+21,cy)
            else {paint.color=Color.rgb(255,211,26);val star=Path();for(i in 0..9){val a=-Math.PI/2+i*Math.PI/5;val rad=if(i%2==0)12f else 5.5f;val x=l+21+Math.cos(a).toFloat()*rad;val y=cy+Math.sin(a).toFloat()*rad;if(i==0)star.moveTo(x,y)else star.lineTo(x,y)};star.close();c.drawPath(star,paint);paint.style=Paint.Style.STROKE;paint.strokeWidth=1.5f;paint.color=Color.BLACK;c.drawPath(star,paint);paint.style=Paint.Style.FILL}
            paint.typeface=Typeface.create("sans-serif",Typeface.BOLD);paint.textAlign=Paint.Align.LEFT;paint.textSize=17f
            val available=r-l-41;val length=paint.measureText(names[index]);if(length>available)paint.textSize*=available/length
            val y=cy-(paint.ascent()+paint.descent())/2
            paint.style=Paint.Style.STROKE;paint.strokeWidth=2.4f;paint.color=Color.WHITE;c.drawText(names[index],l+38,y,paint)
            paint.style=Paint.Style.FILL;paint.color=Color.rgb(15,13,21);c.drawText(names[index],l+38,y,paint)
        }
        names.indices.filter{it!=active}.forEach(::tab)
        if(active in names.indices)tab(active)
    }
    companion object {
        /** Six tabs in the span the original five covered, between the L and R shoulder-button art. */
        const val LEFT=98f
        const val STEP=107f
    }
    private fun drawIcon(c:Canvas,index:Int,x:Float,y:Float) {
        c.save();c.translate(x,y);paint.color=Color.rgb(25,24,34);paint.style=Paint.Style.FILL
        when(index) {
            0->{c.drawRoundRect(RectF(-12f,-10f,12f,7f),2f,2f,paint);paint.color=Color.rgb(79,185,235);c.drawRect(-9f,-7f,9f,4f,paint);paint.color=Color.rgb(224,238,255);c.drawLine(-7f,2f,5f,-6f,paint);paint.color=Color.rgb(25,24,34);c.drawRect(-2f,7f,2f,11f,paint);c.drawRoundRect(RectF(-7f,10f,7f,12f),1f,1f,paint)}
            1->{val p=Path().apply{moveTo(-11f,-4f);lineTo(-5f,-4f);lineTo(2f,-10f);lineTo(2f,10f);lineTo(-5f,4f);lineTo(-11f,4f);close()};c.drawPath(p,paint);paint.style=Paint.Style.STROKE;paint.strokeWidth=2.3f;c.drawArc(RectF(-3f,-9f,12f,9f),-65f,130f,false,paint);c.drawArc(RectF(-1f,-6f,8f,6f),-65f,130f,false,paint);paint.style=Paint.Style.FILL}
            2->{c.drawRoundRect(RectF(-13f,-9f,13f,9f),6f,6f,paint);paint.color=Color.rgb(225,220,239);c.drawRoundRect(RectF(-10f,-6f,10f,6f),4f,4f,paint);paint.color=Color.rgb(25,24,34);c.drawRect(-8f,-1f,-1f,1f,paint);c.drawRect(-5.5f,-3.5f,-3.5f,3.5f,paint);c.drawCircle(5f,-2f,1.5f,paint);c.drawCircle(8f,2f,1.5f,paint)}
            // A headset: visor, two lenses, and the strap either side.
            5->{c.drawRoundRect(RectF(-12f,-7f,12f,7f),4f,4f,paint);c.drawRect(-15f,-3f,-12f,3f,paint);c.drawRect(12f,-3f,15f,3f,paint)
                paint.color=Color.rgb(79,185,235);c.drawCircle(-5.5f,0f,3.6f,paint);c.drawCircle(5.5f,0f,3.6f,paint);paint.color=Color.rgb(25,24,34)}
            3->{paint.style=Paint.Style.STROKE;paint.strokeWidth=6f;paint.strokeCap=Paint.Cap.ROUND;c.drawLine(-8f,9f,5f,-5f,paint);paint.strokeWidth=3f;c.drawArc(RectF(-3f,-12f,12f,3f),20f,285f,false,paint);paint.strokeCap=Paint.Cap.BUTT;paint.style=Paint.Style.FILL}
        }
        c.restore();paint.style=Paint.Style.FILL
    }
}
