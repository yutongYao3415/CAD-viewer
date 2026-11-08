package com.infomatik.dwgviewer

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.max

class DrawingView(context: Context) : View(context) {
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = 1.4f
        strokeJoin = Paint.Join.ROUND
    }
    private var scene: DrawingScene? = null
    private var spaceIndex = 0
    private var scale = 1f
    private var offsetX = 0f
    private var offsetY = 0f
    private var lastX = 0f
    private var lastY = 0f
    private var moved = false
    private var selected: DrawingEntity? = null
    var onSelection: ((DrawingEntity?) -> Unit)? = null

    private val scaler = ScaleGestureDetector(context, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
        override fun onScale(detector: ScaleGestureDetector): Boolean {
            val next = (scale * detector.scaleFactor).coerceIn(0.0001f, 100000f)
            val factor = next / scale
            offsetX = detector.focusX - (detector.focusX - offsetX) * factor
            offsetY = detector.focusY - (detector.focusY - offsetY) * factor
            scale = next
            invalidate()
            return true
        }
    })

    fun setDrawing(value: DrawingScene) {
        scene = value
        spaceIndex = 0
        selected = null
        post { fitToScreen() }
        invalidate()
    }

    fun showSpace(index: Int) {
        val current = scene ?: return
        if (index !in current.spaces.indices) return
        spaceIndex = index
        selected = null
        onSelection?.invoke(null)
        fitToScreen()
    }

    fun refreshLayers() {
        selected = null
        onSelection?.invoke(null)
        invalidate()
    }

    fun fitToScreen() {
        val current = scene ?: return
        if (width <= 0 || height <= 0) return
        val bounds = current.spaces[spaceIndex].bounds
        scale = SceneMath.fitScale(bounds.minX, bounds.minY, bounds.maxX, bounds.maxY,
            width.toDouble(), height.toDouble(), 32.0).toFloat()
        offsetX = width / 2f - ((bounds.minX + bounds.maxX) / 2.0 * scale).toFloat()
        offsetY = height / 2f + ((bounds.minY + bounds.maxY) / 2.0 * scale).toFloat()
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (oldw == 0 || oldh == 0) fitToScreen()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawColor(Color.rgb(18, 24, 32))
        val current = scene ?: return
        val space = current.spaces[spaceIndex]
        for (entity in space.entities) {
            if (!current.isVisible(entity)) continue
            canvas.save()
            val clip = entity.clip
            if (clip != null && clip.size == 4) {
                canvas.clipRect(sx(clip[0]), sy(clip[3]), sx(clip[2]), sy(clip[1]))
            }
            paint.color = if (entity === selected) Color.YELLOW else (entity.color or -0x1000000)
            when (entity.kind) {
                "text" -> drawText(canvas, entity)
                "hatch" -> drawHatch(canvas, entity)
                else -> drawPath(canvas, entity.points, entity.closed, entity.fill)
            }
            canvas.restore()
        }
    }

    private fun drawPath(canvas: Canvas, points: FloatArray, closed: Boolean, fill: Boolean) {
        if (points.size < 4) return
        val path = Path().apply {
            moveTo(sx(points[0]), sy(points[1]))
            for (i in 2 until points.size - 1 step 2) lineTo(sx(points[i]), sy(points[i + 1]))
            if (closed) close()
        }
        paint.style = if (fill) Paint.Style.FILL else Paint.Style.STROKE
        canvas.drawPath(path, paint)
        paint.style = Paint.Style.STROKE
    }

    private fun drawText(canvas: Canvas, entity: DrawingEntity) {
        if (entity.text.isEmpty()) return
        val x = sx(entity.x)
        val y = sy(entity.y)
        canvas.save()
        canvas.rotate((-entity.rotation * 180.0 / PI).toFloat(), x, y)
        paint.style = Paint.Style.FILL
        paint.textSize = max(6f, entity.height * scale)
        entity.text.split('\n').forEachIndexed { index, line ->
            canvas.drawText(line, x, y + index * paint.textSize * 1.25f, paint)
        }
        paint.style = Paint.Style.STROKE
        canvas.restore()
    }

    private fun drawHatch(canvas: Canvas, entity: DrawingEntity) {
        val path = Path().apply { fillType = Path.FillType.EVEN_ODD }
        var left = Float.POSITIVE_INFINITY
        var top = Float.POSITIVE_INFINITY
        var right = Float.NEGATIVE_INFINITY
        var bottom = Float.NEGATIVE_INFINITY
        for (loop in entity.loops) {
            if (loop.size < 6) continue
            path.moveTo(sx(loop[0]), sy(loop[1]))
            for (i in 0 until loop.size - 1 step 2) {
                val x = sx(loop[i]); val y = sy(loop[i + 1])
                if (i > 0) path.lineTo(x, y)
                left = minOf(left, x); right = maxOf(right, x)
                top = minOf(top, y); bottom = maxOf(bottom, y)
            }
            path.close()
        }
        if (!left.isFinite()) return
        if (entity.fill) {
            paint.style = Paint.Style.FILL
            canvas.drawPath(path, paint)
        } else {
            canvas.drawPath(path, paint)
            canvas.save()
            canvas.clipPath(path)
            val cx = (left + right) / 2f
            val cy = (top + bottom) / 2f
            val span = max(right - left, bottom - top) * 2f
            val gap = max(5f, abs(entity.spacing * scale))
            canvas.rotate((-entity.angle * 180.0 / PI).toFloat(), cx, cy)
            var y = cy - span
            var strokes = 0
            while (y <= cy + span && strokes++ < 2000) {
                canvas.drawLine(cx - span, y, cx + span, y, paint)
                y += gap
            }
            canvas.restore()
        }
        paint.style = Paint.Style.STROKE
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        scaler.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastX = event.x; lastY = event.y; moved = false
                return true
            }
            MotionEvent.ACTION_POINTER_DOWN -> moved = true
            MotionEvent.ACTION_POINTER_UP -> {
                moved = true
                val remaining = if (event.actionIndex == 0) 1 else 0
                if (remaining < event.pointerCount) {
                    lastX = event.getX(remaining)
                    lastY = event.getY(remaining)
                }
            }
            MotionEvent.ACTION_MOVE -> if (event.pointerCount == 1 && !scaler.isInProgress) {
                val dx = event.x - lastX
                val dy = event.y - lastY
                if (abs(dx) + abs(dy) > 2f) moved = true
                offsetX += dx; offsetY += dy
                lastX = event.x; lastY = event.y
                invalidate()
            }
            MotionEvent.ACTION_UP -> if (!moved && !scaler.isInProgress) {
                selected = hit(event.x, event.y)
                onSelection?.invoke(selected)
                invalidate()
                performClick()
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun hit(x: Float, y: Float): DrawingEntity? {
        val current = scene ?: return null
        for (entity in current.spaces[spaceIndex].entities.asReversed()) {
            if (!current.isVisible(entity)) continue
            val clip = entity.clip
            if (clip != null && clip.size == 4 &&
                (x < sx(clip[0]) || x > sx(clip[2]) || y < sy(clip[3]) || y > sy(clip[1]))) continue
            if (entity.kind == "text") {
                val tx = sx(entity.x); val ty = sy(entity.y)
                if (x >= tx - 8 && x <= tx + entity.text.length * entity.height * scale &&
                    y >= ty - entity.height * scale && y <= ty + 8) return entity
            }
            val paths = if (entity.kind == "hatch") entity.loops else listOf(entity.points)
            for (points in paths) {
                for (i in 0 until points.size - 3 step 2) {
                    if (SceneMath.segmentDistance(x.toDouble(), y.toDouble(),
                            sx(points[i]).toDouble(), sy(points[i + 1]).toDouble(),
                            sx(points[i + 2]).toDouble(), sy(points[i + 3]).toDouble()) < 16) return entity
                }
            }
            val worldX = (x - offsetX) / scale
            val worldY = (offsetY - y) / scale
            if (entity.kind == "hatch") {
                var inside = false
                for (loop in entity.loops) {
                    if (SceneMath.pointInPolygon(worldX.toDouble(), worldY.toDouble(), loop))
                        inside = !inside
                }
                if (inside) return entity
            } else if (entity.fill &&
                SceneMath.pointInPolygon(worldX.toDouble(), worldY.toDouble(), entity.points)) return entity
        }
        return null
    }

    private fun sx(x: Float): Float = offsetX + x * scale
    private fun sy(y: Float): Float = offsetY - y * scale
}
