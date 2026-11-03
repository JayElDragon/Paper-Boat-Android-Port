package dev.net64.paperboat

import android.content.Context
import android.graphics.Color
import android.graphics.BitmapFactory
import android.graphics.Bitmap
import android.graphics.Rect
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.view.Gravity
import android.view.KeyEvent
import android.widget.Button
import android.widget.FrameLayout
import android.widget.ImageView
import kotlin.math.min

/** The artwork and its hit targets share the same fitted 1672 x 940 rectangle. */
class ArtworkMenu(context: Context, sounds: MenuSounds, onSelect: (Int) -> Unit) : FrameLayout(context) {
    private var selectedIndex = 0
    private var artworkScale = 1f
    private val pointer = ImageView(context).apply {
        setImageResource(R.drawable.menu_hand)
        scaleType = ImageView.ScaleType.FIT_CENTER
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    private val artwork = ImageView(context).apply {
        setImageResource(R.drawable.menu_artwork)
        scaleType = ImageView.ScaleType.FIT_CENTER
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    private val artworkStates by lazy { MenuArtwork.states(resources) }
    // Credit in the bottom-right corner, on the stage floor below the Goomba.
    private val credit = object : android.view.View(context) {
        private val pen = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            typeface = android.graphics.Typeface.create("sans-serif-condensed", android.graphics.Typeface.BOLD)
            textAlign = Paint.Align.RIGHT
        }
        override fun onDraw(canvas: Canvas) {
            val scale = min(width / 1672f, height / 940f)
            val right = (width + 1672 * scale) / 2 - 14 * scale
            val baseline = (height + 940 * scale) / 2 - 10 * scale
            pen.textSize = 19 * scale
            pen.style = Paint.Style.STROKE; pen.strokeWidth = 4 * scale; pen.strokeJoin = Paint.Join.ROUND
            pen.color = Color.rgb(40, 24, 16)
            canvas.drawText(CREDIT, right, baseline, pen)
            pen.style = Paint.Style.FILL; pen.color = Color.WHITE
            canvas.drawText(CREDIT, right, baseline, pen)
        }
    }.apply { importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO }
    private val targets = listOf("Play", "Options", "Game Files", "Cheats", "Quit").mapIndexed { index, label ->
        Button(context).apply {
            text = ""
            contentDescription = label
            isFocusable = true
            isFocusableInTouchMode = false
            if (android.os.Build.VERSION.SDK_INT >= 26) defaultFocusHighlightEnabled = false
            minimumWidth = 0
            minimumHeight = 0
            setPadding(0, 0, 0, 0)
            setBackgroundColor(Color.TRANSPARENT)
            isSoundEffectsEnabled = false
            setOnClickListener { requestFocus(); movePointer(index); sounds.select(); onSelect(index) }
            setOnTouchListener { _,event ->
                if(event.action==android.view.MotionEvent.ACTION_DOWN)movePointer(index)
                false
            }
            setOnFocusChangeListener { _, focused ->
                if (focused) sounds.move()
                if (focused) movePointer(index)
                setBackgroundColor(Color.TRANSPARENT)
            }
            setOnKeyListener { _, code, event ->
                if (code == KeyEvent.KEYCODE_BUTTON_A || code == KeyEvent.KEYCODE_BUTTON_START) {
                    if (event.action == KeyEvent.ACTION_UP) performClick()
                    true
                } else false
            }
        }
    }

    init {
        setBackgroundColor(Color.BLACK)
        addView(artwork, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        targets.forEach { addView(it) }
        addView(pointer)
        // Last, so the targets keep child indices 1..5; it takes no touches.
        addView(credit, LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT))
        targets.forEachIndexed { i, button ->
            button.id = android.view.View.generateViewId()
        }
        targets.forEachIndexed { i, button ->
            button.nextFocusUpId = targets[(i + 4) % 5].id
            button.nextFocusDownId = targets[(i + 1) % 5].id
        }
        targets.first().requestFocus()
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = MeasureSpec.getSize(heightMeasureSpec)
        val scale = min(w / 1672f, h / 940f)
        artworkScale = scale
        val left = (w - 1672 * scale) / 2
        val top = (h - 940 * scale) / 2
        val rows = listOf(456 to 552, 565 to 638, 648 to 721, 733 to 806, 820 to 896)
        targets.forEachIndexed { i, button ->
            button.layoutParams = LayoutParams((405 * scale).toInt(), ((rows[i].second - rows[i].first) * scale).toInt(), Gravity.TOP or Gravity.LEFT).apply {
                leftMargin = (left + 112 * scale).toInt()
                topMargin = (top + rows[i].first * scale).toInt()
            }
        }
        pointer.layoutParams = LayoutParams((115 * scale).toInt(), (100 * scale).toInt(), Gravity.TOP or Gravity.LEFT).apply {
            leftMargin = (left + 35 * scale).toInt()
            topMargin = (top + 454 * scale).toInt()
        }
        movePointer(selectedIndex, false)
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
    }

    private companion object {
        const val CREDIT = "Unofficial port made by JayElDragon"
    }

    private fun movePointer(index: Int, animate: Boolean = true) {
        selectedIndex = index
        artwork.setImageBitmap(artworkStates[index])
        val centers = floatArrayOf(504f, 601.5f, 684.5f, 769.5f, 858f)
        val y = (centers[index] - 504f) * artworkScale
        pointer.animate().cancel()
        if (animate) pointer.animate().translationY(y).setDuration(110).start()
        else pointer.translationY = y
    }
}

