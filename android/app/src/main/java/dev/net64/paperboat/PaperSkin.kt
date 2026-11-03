package dev.net64.paperboat

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Rect

/** Changes paper pigment in place; no rectangular overlays or cropped borders. */
object PaperSkin {
    fun selected(source:Bitmap, regions:List<Rect>, active:Int, originallyYellow:Int):Bitmap {
        val out=source.copy(Bitmap.Config.ARGB_8888,true)
        val pixels=IntArray(out.width*out.height)
        out.getPixels(pixels,0,out.width,0,0,out.width,out.height)
        regions.forEachIndexed { index,area ->
            if((index==active)==(index==originallyYellow)) return@forEachIndexed
            val width=area.width();val height=area.height();val count=width*height
            val candidate=BooleanArray(count);val visited=BooleanArray(count);val queue=IntArray(count)
            for(y in 0 until height) for(x in 0 until width){
                val c=pixels[(area.top+y)*out.width+area.left+x]
                val r=Color.red(c);val g=Color.green(c);val b=Color.blue(c)
                candidate[y*width+x]=if(index==originallyYellow)
                    r>150&&g>110&&b<170&&r>b*1.15f&&g>b*1.1f
                else maxOf(r,g,b)-minOf(r,g,b)<85&&(r+g+b)/3 in 95..255
            }
            var largest=IntArray(0)
            for(seed in 0 until count){
                if(!candidate[seed]||visited[seed])continue
                var begin=0;var end=1;queue[0]=seed;visited[seed]=true
                while(begin<end){
                    val p=queue[begin++];val x=p%width;val y=p/width
                    fun add(n:Int){if(candidate[n]&&!visited[n]){visited[n]=true;queue[end++]=n}}
                    if(x>0)add(p-1);if(x+1<width)add(p+1);if(y>0)add(p-width);if(y+1<height)add(p+width)
                }
                if(end>largest.size)largest=queue.copyOf(end)
            }
            // Only the connected paper surface is tinted; scenery, icons and glyphs stay intact.
            for(local in largest){
                    val at=(area.top+local/width)*out.width+area.left+local%width;val c=pixels[at]
                    val r=Color.red(c);val g=Color.green(c);val b=Color.blue(c);val alpha=Color.alpha(c)
                    if(index==originallyYellow&&r>150&&g>110&&b<170&&r>b*1.15f&&g>b*1.1f) {
                        val v=(((r+g)/2f)*.42f+119).toInt().coerceIn(172,230)
                        pixels[at]=Color.argb(alpha,v,v-5,(v+12).coerceAtMost(255))
                    } else if(index==active&&maxOf(r,g,b)-minOf(r,g,b)<85&&(r+g+b)/3 in 95..255) {
                        val v=(r+g+b)/3
                        pixels[at]=Color.argb(alpha,(210+v*.18f).toInt().coerceAtMost(255),(145+v*.35f).toInt().coerceAtMost(238),(15+v*.20f).toInt())
                    }
            }
        }
        out.setPixels(pixels,0,out.width,0,0,out.width,out.height)
        return out
    }
}
