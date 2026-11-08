package com.infomatik.dwgviewer

import org.json.JSONArray
import org.json.JSONObject

data class DrawingLayer(val name: String, var visible: Boolean, val color: Int)

data class DrawingBounds(val minX: Double, val minY: Double, val maxX: Double, val maxY: Double) {
    val width: Double get() = maxX - minX
    val height: Double get() = maxY - minY
}

data class DrawingEntity(
    val kind: String,
    val type: String,
    val handle: String,
    val layer: String,
    val ownerLayer: String = "",
    val color: Int,
    val points: FloatArray = floatArrayOf(),
    val loops: List<FloatArray> = emptyList(),
    val closed: Boolean = false,
    val fill: Boolean = false,
    val text: String = "",
    val x: Float = 0f,
    val y: Float = 0f,
    val height: Float = 1f,
    val rotation: Float = 0f,
    val clip: FloatArray? = null,
    val spacing: Float = 0f,
    val angle: Float = 0f
)

class DrawingSpace(val name: String, val entities: List<DrawingEntity>) {
    val bounds: DrawingBounds by lazy {
        var minX = Double.POSITIVE_INFINITY
        var minY = Double.POSITIVE_INFINITY
        var maxX = Double.NEGATIVE_INFINITY
        var maxY = Double.NEGATIVE_INFINITY
        fun include(x: Double, y: Double) {
            if (x.isFinite() && y.isFinite()) {
                minX = minOf(minX, x); minY = minOf(minY, y)
                maxX = maxOf(maxX, x); maxY = maxOf(maxY, y)
            }
        }
        entities.forEach { entity ->
            entity.clip?.takeIf { it.size == 4 }?.let { clip ->
                include(clip[0].toDouble(), clip[1].toDouble())
                include(clip[2].toDouble(), clip[3].toDouble())
                return@forEach
            }
            if (entity.kind == "text") {
                include(entity.x.toDouble(), entity.y.toDouble())
                include((entity.x + entity.height * entity.text.length * 0.6).toDouble(),
                    (entity.y + entity.height).toDouble())
            }
            (listOf(entity.points) + entity.loops).forEach { points ->
                for (i in 0 until points.size - 1 step 2) include(points[i].toDouble(), points[i + 1].toDouble())
            }
        }
        if (!minX.isFinite()) DrawingBounds(0.0, 0.0, 1.0, 1.0)
        else DrawingBounds(minX, minY, maxX, maxY)
    }
}

class DrawingScene(val layers: List<DrawingLayer>, val spaces: List<DrawingSpace>) {
    fun isVisible(entity: DrawingEntity): Boolean =
        (layers.firstOrNull { it.name == entity.layer }?.visible ?: true) &&
        (entity.ownerLayer.isEmpty() ||
            (layers.firstOrNull { it.name == entity.ownerLayer }?.visible ?: true))

    companion object {
        fun fromJson(json: String): DrawingScene {
            val root = JSONObject(json)
            if (root.has("error")) throw IllegalArgumentException(root.getString("error"))
            val layersJson = root.getJSONArray("layers")
            val layers = (0 until layersJson.length()).map { index ->
                val item = layersJson.getJSONObject(index)
                DrawingLayer(item.getString("name"), item.optBoolean("visible", true), item.optInt("color", 0xffffff))
            }
            val spacesJson = root.getJSONArray("spaces")
            val spaces = (0 until spacesJson.length()).map { index ->
                val item = spacesJson.getJSONObject(index)
                val entitiesJson = item.getJSONArray("entities")
                DrawingSpace(item.getString("name"), (0 until entitiesJson.length()).map { entityIndex ->
                    parseEntity(entitiesJson.getJSONObject(entityIndex))
                })
            }
            if (spaces.isEmpty()) throw IllegalArgumentException("图纸中没有可显示的空间")
            return DrawingScene(layers, spaces)
        }

        private fun parseEntity(item: JSONObject): DrawingEntity {
            val loopsJson = item.optJSONArray("loops") ?: JSONArray()
            val clipJson = item.optJSONArray("clip")
            return DrawingEntity(
                kind = item.getString("kind"),
                type = item.optString("type", ""),
                handle = item.optString("handle", ""),
                layer = item.optString("layer", "0"),
                ownerLayer = item.optString("ownerLayer", ""),
                color = item.optInt("color", 0xffffff),
                points = floats(item.optJSONArray("points")),
                loops = (0 until loopsJson.length()).map { floats(loopsJson.getJSONArray(it)) },
                closed = item.optBoolean("closed"),
                fill = item.optBoolean("fill"),
                text = item.optString("text", ""),
                x = item.optDouble("x", 0.0).toFloat(),
                y = item.optDouble("y", 0.0).toFloat(),
                height = item.optDouble("height", 1.0).toFloat(),
                rotation = item.optDouble("rotation", 0.0).toFloat(),
                clip = clipJson?.let(::floats),
                spacing = item.optDouble("spacing", 0.0).toFloat(),
                angle = item.optDouble("angle", 0.0).toFloat()
            )
        }

        private fun floats(array: JSONArray?): FloatArray =
            if (array == null) floatArrayOf() else FloatArray(array.length()) { array.getDouble(it).toFloat() }
    }
}
