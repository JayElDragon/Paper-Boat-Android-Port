package dev.net64.paperboat

import android.content.res.Resources
import android.graphics.*

/** Compose local artwork edits and derive selection from the paper's actual edge. */
object MenuArtwork {
    fun states(resources:Resources):List<Bitmap> {
        val source=Bitmap.createScaledBitmap(BitmapFactory.decodeResource(resources,R.drawable.menu_artwork),1672,940,true).copy(Bitmap.Config.ARGB_8888,true)
        val canvas=Canvas(source)
        val pen=Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        fun patch(id:Int,destination:RectF,clip:RectF=destination) {
            canvas.save();canvas.clipRect(clip)
            canvas.drawBitmap(BitmapFactory.decodeResource(resources,id),null,destination,pen);canvas.restore()
        }
        patch(R.drawable.menu_hill_clean,RectF(790f,190f,1080f,450f))
        // Only the eye interiors are replaced; the rest of the character is untouched.
        patch(R.drawable.menu_eyes_clean,RectF(1270f,720f,1600f,930f),RectF(1418f,741f,1500f,791f))
        patch(R.drawable.menu_rays_clean,RectF(480f,420f,568f,510f))
        val regions=listOf(Rect(104,448,540,558),Rect(104,560,530,643),Rect(104,643,530,729),Rect(104,728,530,813),Rect(100,812,530,905))
        return (0..4).map { index ->
            val state=PaperSkin.selected(source,regions,index,0)
            if(index!=0) {
                for(y in 453..552) for(x in 108..520) {
                    if(y in 474..535&&x in 151..485)continue
                    val c=state.getPixel(x,y)
                    if(Color.red(c)>155&&Color.red(c)>Color.green(c)*1.55f&&Color.green(c)<140&&Color.blue(c)<135)
                        state.setPixel(x,y,Color.rgb(218,211,228))
                }
                outline(state,source,regions[index])
            }
            val shift=floatArrayOf(0f,99f,182f,267f,355f)[index]
            val target=Canvas(state)
            val rays=listOf(floatArrayOf(499f,435f,513f,441f,506f,462f,496f,458f),
                floatArrayOf(532f,447f,542f,459f,524f,473f,518f,466f),
                floatArrayOf(548f,480f,550f,491f,529f,494f,528f,484f))
            rays.forEach { p ->
                val path=Path().apply{moveTo(p[0],p[1]+shift);for(i in 2 until p.size step 2)lineTo(p[i],p[i+1]+shift);close()}
                pen.style=Paint.Style.STROKE;pen.strokeJoin=Paint.Join.ROUND;pen.color=Color.WHITE;pen.strokeWidth=7f;target.drawPath(path,pen)
                pen.style=Paint.Style.FILL;pen.color=Color.rgb(224,35,21);target.drawPath(path,pen)
            }
            state
        }
    }
    /** Repaints an artwork's baked red selection edge inside [area] with [paper]. */
    fun clearRed(output:Bitmap,area:Rect,paper:Int) {
        for(y in area.top until area.bottom) for(x in area.left until area.right) {
            val c=output.getPixel(x,y)
            if(Color.red(c)>155&&Color.red(c)>Color.green(c)*1.55f&&Color.green(c)<140&&Color.blue(c)<135)
                output.setPixel(x,y,Color.argb(Color.alpha(c),Color.red(paper),Color.green(paper),Color.blue(paper)))
        }
    }

    /**
     * Options-style scenes for artwork whose first button is drawn selected:
     * the baked red edge is cleared and redrawn on whichever button is active,
     * as on the main menu. [band] is the outline's distance inside the paper.
     */
    fun selectionScenes(source:Bitmap,areas:List<Rect>,band:IntRange,firstDrawnSelected:Boolean=true):List<Bitmap> {
        // Artwork whose buttons are all plain paper: tint and outline the active one only.
        if(!firstDrawnSelected) return areas.indices.map { index ->
            PaperSkin.selected(source,areas,index,-1).also{outline(it,source,areas[index],band)}
        }
        val lavender=Color.rgb(218,211,228);val yellow=Color.rgb(247,207,64)
        // The first button's paper as plain lavender, so its outline is found the same way.
        val firstPlain=PaperSkin.selected(source,areas,1,0).also{clearRed(it,areas[0],lavender)}
        return areas.indices.map { index ->
            PaperSkin.selected(source,areas,index,0).also { state ->
                clearRed(state,areas[0],if(index==0)yellow else lavender)
                outline(state,if(index==0)firstPlain else source,areas[index],band)
            }
        }
    }

    fun outline(output:Bitmap,source:Bitmap,area:Rect,band:IntRange=4..7) {
        val w=area.width();val h=area.height();val pixels=IntArray(w*h)
        source.getPixels(pixels,0,w,area.left,area.top,w,h)
        val candidates=BooleanArray(w*h) { i ->
            val c=pixels[i];val r=Color.red(c);val g=Color.green(c);val b=Color.blue(c)
            r>125&&g>120&&b>135&&maxOf(r,g,b)-minOf(r,g,b)<85
        }
        val seen=BooleanArray(w*h);val queue=IntArray(w*h);var component=IntArray(0)
        for(seed in candidates.indices) {
            if(!candidates[seed]||seen[seed])continue
            var begin=0;var end=1;queue[0]=seed;seen[seed]=true
            while(begin<end) {
                val p=queue[begin++];val x=p%w;val y=p/w
                fun add(n:Int){if(candidates[n]&&!seen[n]){seen[n]=true;queue[end++]=n}}
                if(x>0)add(p-1);if(x+1<w)add(p+1);if(y>0)add(p-w);if(y+1<h)add(p+w)
            }
            if(end>component.size)component=queue.copyOf(end)
        }
        // Fill only enclosed glyph/icon holes. The exterior contour comes from the artwork.
        val lo=IntArray(h){w};val hi=IntArray(h){-1}
        component.forEach { p ->val y=p/w;lo[y]=minOf(lo[y],p%w);hi[y]=maxOf(hi[y],p%w)}
        val distance=IntArray(w*h)
        for(y in 0 until h) for(x in 0 until w)if(x in lo[y]..hi[y])distance[y*w+x]=999
        for(y in 0 until h)for(x in 0 until w) {
            val i=y*w+x;if(distance[i]==0)continue
            distance[i]=minOf(distance[i],if(x>0)distance[i-1]+1 else 1,if(y>0)distance[i-w]+1 else 1)
        }
        for(y in h-1 downTo 0)for(x in w-1 downTo 0) {
            val i=y*w+x;if(distance[i]==0)continue
            distance[i]=minOf(distance[i],if(x+1<w)distance[i+1]+1 else 1,if(y+1<h)distance[i+w]+1 else 1)
            if(distance[i] in band && candidates[i])output.setPixel(area.left+x,area.top+y,Color.rgb(223,31,22))
        }
    }
}
