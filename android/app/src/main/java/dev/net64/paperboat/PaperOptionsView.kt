package dev.net64.paperboat

import android.content.Context
import android.graphics.*
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import kotlin.math.abs
import kotlin.math.min

/** One complete scene per page; selection only changes the paper pigment. */
class PaperOptionsView(context:Context, initial:String, private val sounds:MenuSounds,
    private val action:(String)->Unit, private val returnToShortcut:Boolean=false):View(context) {
    private val names=listOf("Graphics","Audio","Controls","System","Cheats","VR Settings")
    private var page=initial
    private var selected=0
    private val positions=mutableMapOf<String,Int>()
    private val paint=Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private fun bitmap(id:Int)=Bitmap.createScaledBitmap(BitmapFactory.decodeResource(resources,id),833,466,true)
    private val home=bitmap(R.drawable.options_home_clean)
    private val panel=bitmap(R.drawable.settings_scene)
    private val cleanPanel=BitmapFactory.decodeResource(resources,R.drawable.settings_tabs_background)
    private val tabs=PaperTabs(resources)
    private val rowPaper by lazy { val art=BitmapFactory.decodeResource(resources,R.drawable.cheats_artwork);BitmapShader(Bitmap.createBitmap(art,835,204,180,44),Shader.TileMode.REPEAT,Shader.TileMode.REPEAT) }
    private val files=bitmap(R.drawable.files_panel)
    private val controls=bitmap(R.drawable.controls_panel)
    private val cheatArtwork=BitmapFactory.decodeResource(resources,R.drawable.cheats_artwork)
    private val homeAreas=listOf(Rect(342,113,714,186),Rect(342,186,714,253),Rect(342,253,714,319),Rect(342,319,714,386))
    private val tabAreas=names.indices.map{Rect((PaperTabs.LEFT+it*PaperTabs.STEP).toInt(),61,(PaperTabs.LEFT+(it+1)*PaperTabs.STEP).toInt(),118)}
    private var cheatsTabSelected=false
    private val fileAreas=(0..3).map{Rect(210,133+it*60,565,193+it*60)}
    // The red edge follows the selection, as on the main menu (half its band: half the scale).
    // options_home_clean has all four buttons as plain paper (Graphics rebuilt to match).
    private val homeScenes by lazy{MenuArtwork.selectionScenes(home,homeAreas,2..3,firstDrawnSelected=false)}
    private val fileScenes by lazy{MenuArtwork.selectionScenes(files,fileAreas,2..3)}
    private var scale=1f;private var offsetX=0f;private var offsetY=0f
    // The paper fits seven rows. Longer pages scroll by pixel, as in Poke
    // Survivor's lists: drag anywhere on the list (a tap still selects), fling,
    // or drag the bar's thumb. Controller input scrolls just enough to keep the
    // selection whole.
    private var scrollY=0f
    private fun maxScroll()=((entries().size-VISIBLE_ROWS)*ROW_H).coerceAtLeast(0f)
    private fun clampScroll(){scrollY=scrollY.coerceIn(0f,maxScroll())}
    private fun keepSelectionVisible(){
        val top=selected*ROW_H
        if(top<scrollY)scrollY=top
        if(top+ROW_H>scrollY+VISIBLE_ROWS*ROW_H)scrollY=top+ROW_H-VISIBLE_ROWS*ROW_H
        clampScroll()
    }
    private fun rowAt(y:Float)=((y-LIST_TOP+scrollY)/ROW_H).toInt().coerceIn(0,entries().lastIndex)
    private val barArea get()=RectF(588f,LIST_TOP+2,598f,LIST_BOTTOM-2)
    private fun thumbArea():RectF{
        val track=barArea
        val height=(track.height()*VISIBLE_ROWS/entries().size).coerceAtLeast(26f)
        val top=track.top+(track.height()-height)*(if(maxScroll()>0)scrollY/maxScroll() else 0f)
        return RectF(track.left,top,track.right,top+height)
    }
    private fun scrollFromBar(y:Float){
        val track=barArea;val thumb=thumbArea().height()
        scrollY=((y-track.top-thumb/2)/(track.height()-thumb)).coerceIn(0f,1f)*maxScroll()
        invalidate()
    }
    private fun drawScrollBar(c:Canvas){
        if(maxScroll()<=0f)return
        paint.style=Paint.Style.FILL
        paint.color=Color.argb(70,25,23,36);c.drawRoundRect(barArea,5f,5f,paint)
        val thumb=thumbArea()
        paint.color=if(thumbDrag)Color.rgb(246,196,51) else Color.rgb(250,214,74);c.drawRoundRect(thumb,5f,5f,paint)
        paint.style=Paint.Style.STROKE;paint.strokeWidth=1.2f;paint.color=Color.rgb(220,48,24)
        c.drawRoundRect(thumb,5f,5f,paint);paint.style=Paint.Style.FILL
    }
    private var downY=0f;private var lastY=0f;private var lastTime=0L
    private var dragging=false;private var thumbDrag=false;private var velocity=0f
    private val dragThreshold=10*resources.displayMetrics.density
    private val fling=object:Runnable{
        override fun run(){
            if(dragging||abs(velocity)<.02f)return
            val before=scrollY
            scrollY-=velocity*16f;velocity*=.92f;clampScroll()
            if(scrollY==before)velocity=0f
            invalidate();postOnAnimation(this)
        }
    }
    /** Drag, fling and scrollbar for list pages; false lets a plain tap through. */
    private fun listTouch(event:MotionEvent,x:Float,y:Float):Boolean {
        when(event.actionMasked){
            MotionEvent.ACTION_DOWN->{
                removeCallbacks(fling);velocity=0f;dragging=false
                downY=y;lastY=y;lastTime=event.eventTime
                thumbDrag=maxScroll()>0f&&x>=582f&&x<=606f&&y in LIST_TOP..LIST_BOTTOM
                if(thumbDrag)scrollFromBar(y)
                else if(x in 55f..585f&&y in LIST_TOP..LIST_BOTTOM)select(rowAt(y))
                return true
            }
            MotionEvent.ACTION_MOVE->{
                if(thumbDrag){scrollFromBar(y);return true}
                if(!dragging&&maxScroll()>0f&&downY in LIST_TOP..LIST_BOTTOM&&abs(y-downY)*scale>=dragThreshold){dragging=true;lastY=y}
                if(dragging){
                    val dt=(event.eventTime-lastTime).coerceAtLeast(1L)
                    velocity=velocity*.3f+(y-lastY)/dt*.7f
                    scrollY-=y-lastY;clampScroll();lastY=y;lastTime=event.eventTime;invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP->{
                if(thumbDrag){thumbDrag=false;invalidate();return true}
                // Releasing a drag must not press the row under the finger.
                if(dragging){dragging=false;if(event.eventTime-lastTime>80)velocity=0f;postOnAnimation(fling);return true}
                return false
            }
            MotionEvent.ACTION_CANCEL->{dragging=false;thumbDrag=false;invalidate();return true}
        }
        return true
    }
    private fun entries()=SettingsCatalog.rows(page)
    init {isFocusableInTouchMode=true;isSoundEffectsEnabled=false;requestFocus()}

    private fun text(c:Canvas,label:String,x:Float,y:Float,size:Float=18f,center:Boolean=false) {
        paint.color=Color.rgb(15,14,20);paint.textSize=size
        paint.typeface=Typeface.create("sans-serif-condensed",Typeface.BOLD)
        paint.textAlign=if(center)Paint.Align.CENTER else Paint.Align.LEFT
        c.drawText(label,x,y,paint)
    }
    override fun onDraw(c:Canvas) {
        c.drawColor(Color.BLACK)
        scale=min(width/833f,height/466f);offsetX=(width-833*scale)/2;offsetY=(height-466*scale)/2
        c.save();c.translate(offsetX,offsetY);c.scale(scale,scale)
        val background=when(page){"home"->homeScenes[selected.coerceIn(0,3)];"files"->fileScenes[selected.coerceIn(0,3)];else->cleanPanel}
        paint.color=Color.WHITE;c.drawBitmap(background,null,RectF(0f,0f,833f,466f),paint)
        if(isList())tabs.draw(c,if(cheatsTabSelected)4 else names.indexOf(tabPage()))
        when(page) {
            "home" -> Unit // No fixed or floating glove in Options.
            "files" -> drawFileInfo(c)
            else -> {
                val rows=entries()
                clampScroll()
                c.save();c.clipRect(50f,LIST_TOP-1,586f,LIST_BOTTOM+1)
                rows.forEachIndexed{i,row->
                    val y=151f+i*ROW_H-scrollY
                    if(y<LIST_TOP-ROW_H||y>LIST_BOTTOM+ROW_H)return@forEachIndexed
                    if(i==selected) {
                        val p=Path().apply{moveTo(57f,y);lineTo(69f,y-16);lineTo(571f,y-16);lineTo(583f,y);lineTo(571f,y+16);lineTo(69f,y+16);close()}
                        paint.shader=rowPaper;c.drawPath(p,paint);paint.shader=null
                        paint.style=Paint.Style.STROKE;paint.color=Color.WHITE;paint.strokeWidth=2f;c.drawPath(p,paint)
                        paint.color=Color.rgb(220,48,24);paint.strokeWidth=.7f;c.drawPath(p,paint);paint.style=Paint.Style.FILL
                    }
                    text(c,row.title,77f,y+6,18f)
                    val label=if(row.key.isEmpty()) "Open  ›" else {
                        val value=EngineSettings.value(context,row.key,row.default,row.floating)
                        val index=row.values.indexOfFirst{abs(it-value)<.001f}
                        "‹  ${row.labels.getOrNull(index) ?: value.toString()}  ›"
                    }
                    if(page=="Cheats") {
                        val on=EngineSettings.value(context,row.key,row.default)!=0f
                        paint.color=Color.rgb(25,23,36);c.drawRoundRect(RectF(424f,y-11,496f,y+12),11f,11f,paint)
                        paint.color=if(on)Color.rgb(12,190,42)else Color.rgb(80,79,96);c.drawRoundRect(RectF(426f,y-9,494f,y+10),9f,9f,paint)
                        paint.color=Color.rgb(235,235,243);c.drawCircle(if(on)484f else 436f,y+.5f,8f,paint)
                        text(c,if(on)"ON"else"OFF",if(on)451f else 465f,y+6,15f,true)
                    } else text(c,label,457f,y+6,17f,true)
                }
                c.restore()
                drawScrollBar(c)
                if(page=="Controls") {
                    paint.color=Color.WHITE
                    c.drawBitmap(controls,Rect(480,137,810,362),RectF(600f,148f,793f,345f),paint)
                }
                if(page=="Cheats") {
                    paint.color=Color.WHITE
                    c.drawBitmap(cheatArtwork,Rect(1308,238,1655,708),RectF(600f,138f,798f,377f),paint)
                }
                text(c,rows.getOrNull(selected)?.help?.take(72) ?: "",414f,431f,13f,true)
            }
        }
        c.restore()
    }
    private fun openCheats(){navigate("Cheats");sounds.move()}
    private fun drawFileInfo(c:Canvas) {
        val rom=GameAssets.romFile(context);val ready=rom.isFile
        // Reuse a paper texture for live metadata, never a flat colored rectangle.
        paint.color=Color.WHITE
        c.drawBitmap(panel,Rect(220,230,420,330),RectF(583f,282f,789f,386f),paint)
        text(c,if(ready)"Paper Mario (USA)" else "No ROM installed",595f,305f,16f)
        text(c,if(ready)"Size: ${rom.length()/1048576} MB" else "Select your own ROM",595f,328f,15f)
        text(c,if(ready&&GameAssets.isExtracted(context))"Installed" else "Not extracted",595f,352f,16f)
    }
    private fun select(index:Int){if(index!=selected){selected=index;sounds.move();invalidate()}}
    private fun navigate(next:String){
        cheatsTabSelected=false
        if(page=="home")positions["home"]=selected else positions[page]=selected
        if(next=="home"&&page in names&&names.indexOf(page)<4)positions["home"]=names.indexOf(page)
        page=next;selected=(positions[next]?:0).coerceIn(0,if(next=="home"||next=="files")3 else entries().lastIndex)
        removeCallbacks(fling);velocity=0f;scrollY=0f;keepSelectionVisible()
        invalidate()
    }
    private fun adjust(direction:Int){
        val row=entries().getOrNull(selected)?:return
        if(row.key.isEmpty()){
            if(direction<=0)return
            sounds.select()
            when{
                row.action.startsWith("page:")->navigate(row.action.removePrefix("page:"))
                // Same as the PC "Reset Status Bar": every keyed row back to its default.
                row.action=="reset"->{
                    entries().filter{it.key.isNotEmpty()}.forEach{runCatching{EngineSettings.change(context,it.key,it.default,it.floating)}}
                    invalidate()
                }
                else->action(row.action)
            }
            return
        }
        val old=EngineSettings.value(context,row.key,row.default,row.floating)
        val at=row.values.indexOfFirst{abs(it-old)<.001f}.coerceAtLeast(0)
        val value=row.values[(at+direction+row.values.size)%row.values.size]
        runCatching{EngineSettings.change(context,row.key,value,row.floating)}.onSuccess{
            sounds.select()
            if(row.restart)android.widget.Toast.makeText(context,"${row.title}: applies after Restart Game (System).",android.widget.Toast.LENGTH_SHORT).show()
        }.onFailure{sounds.error()}
        invalidate()
    }
    private companion object {
        val SUBPAGES=mapOf("StatusBar" to "Graphics")
        const val VISIBLE_ROWS=7
        const val ROW_H=34f
        const val LIST_TOP=134f
        const val LIST_BOTTOM=LIST_TOP+VISIBLE_ROWS*ROW_H
    }
    private fun choose(){when(page){"home"->{sounds.select();navigate(names[selected])};"files"->{sounds.select();action(listOf("verify","import","verify","clear")[selected])};else->adjust(1)}}
    private fun back(){
        val parent=SUBPAGES[page]
        if(parent!=null){navigate(parent);sounds.move()}
        else if(returnToShortcut||page=="home"||page=="files")action("back")else navigate("home")
    }
    /** Option lists: the six tabs plus subpages, which keep their parent's tab raised. */
    private fun isList()=page in names||page in SUBPAGES
    private fun tabPage()=SUBPAGES[page]?:page
    override fun onKeyDown(key:Int,event:KeyEvent):Boolean {
        val count=if(page=="home"||page=="files")4 else entries().size
        when(key){
            KeyEvent.KEYCODE_DPAD_UP->{select((selected+count-1)%count);if(isList()){keepSelectionVisible();invalidate()}}
            KeyEvent.KEYCODE_DPAD_DOWN->{select((selected+1)%count);if(isList()){keepSelectionVisible();invalidate()}}
            KeyEvent.KEYCODE_DPAD_LEFT->if(isList())adjust(-1)
            KeyEvent.KEYCODE_DPAD_RIGHT->if(isList())adjust(1)
            KeyEvent.KEYCODE_BUTTON_L1,KeyEvent.KEYCODE_BUTTON_R1->{
                val at=if(cheatsTabSelected)4 else names.indexOf(tabPage()).coerceAtLeast(0)
                val next=(at+if(key==KeyEvent.KEYCODE_BUTTON_L1)names.size-1 else 1)%names.size
                if(next==4)openCheats()else{navigate(names[next]);sounds.move()}
            }
            KeyEvent.KEYCODE_BUTTON_A,KeyEvent.KEYCODE_ENTER,KeyEvent.KEYCODE_DPAD_CENTER->choose()
            KeyEvent.KEYCODE_BACK,KeyEvent.KEYCODE_BUTTON_B->back()
            else->return super.onKeyDown(key,event)
        };return true
    }
    override fun onTouchEvent(event:MotionEvent):Boolean {
        val x=(event.x-offsetX)/scale;val y=(event.y-offsetY)/scale
        if(isList()&&listTouch(event,x,y))return true
        if(event.action==MotionEvent.ACTION_DOWN||event.action==MotionEvent.ACTION_MOVE){
            when(page){
                "home"->homeAreas.indexOfFirst{it.contains(x.toInt(),y.toInt())}.takeIf{it>=0}?.let(::select)
                "files"->fileAreas.indexOfFirst{it.contains(x.toInt(),y.toInt())}.takeIf{it>=0}?.let(::select)
                else->if(x in 55f..585f&&y in 133f..386f)select(rowAt(y))
            };return true
        }
        if(event.action!=MotionEvent.ACTION_UP)return true
        if(y>402&&x<150){back();return true}
        if(page=="home"&&homeAreas.any{it.contains(x.toInt(),y.toInt())})choose()
        else if(page=="files"&&fileAreas.any{it.contains(x.toInt(),y.toInt())})choose()
        else if(isList()){
            val tab=tabAreas.indexOfFirst{it.contains(x.toInt(),y.toInt())}
            if(tab==4)openCheats()
            else if(tab>=0){navigate(names[tab]);sounds.move()}
            else if(x in 55f..585f&&y in LIST_TOP..LIST_BOTTOM)adjust(if(x in 335f..405f)-1 else 1)
            else if(y>402&&x>700)choose()
        }
        return true
    }
}
