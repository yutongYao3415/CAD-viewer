package com.infomatik.dwgviewer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DrawingSceneTest {
    @Test fun parsesSpacesLayersAndEntityMetadata() {
        val scene = DrawingScene.fromJson(
            """{"layers":[{"name":"0","visible":true,"color":16777215}],"spaces":[{"name":"模型","entities":[{"kind":"path","type":"LINE","handle":"1A","layer":"0","color":16777215,"points":[0,0,10,0],"closed":false,"fill":false}]}]}"""
        )
        assertEquals("模型", scene.spaces.single().name)
        assertEquals("1A", scene.spaces.single().entities.single().handle)
        assertEquals(10.0, scene.spaces.single().bounds.maxX, 0.001)
        assertTrue(scene.layers.single().visible)
    }

    @Test fun rejectsNativeError() {
        try {
            DrawingScene.fromJson("""{"error":"图纸损坏"}""")
            throw AssertionError("error must be shown")
        } catch (expected: IllegalArgumentException) {
            assertEquals("图纸损坏", expected.message)
        }
    }

    @Test fun hiddenLayerIsExcludedFromVisibleEntities() {
        val scene = DrawingScene.fromJson(
            """{"layers":[{"name":"A","visible":false,"color":1}],"spaces":[{"name":"模型","entities":[{"kind":"path","type":"LINE","handle":"1","layer":"A","color":1,"points":[0,0,1,1]}]}]}"""
        )
        assertFalse(scene.isVisible(scene.spaces.single().entities.single()))
        scene.layers.single().visible = true
        assertTrue(scene.isVisible(scene.spaces.single().entities.single()))
    }

    @Test fun hidingInsertLayerHidesBlockGeometry() {
        val scene = DrawingScene.fromJson(
            """{"layers":[{"name":"A","visible":true},{"name":"B","visible":false}],"spaces":[{"name":"模型","entities":[{"kind":"path","type":"INSERT","handle":"2","layer":"A","ownerLayer":"B","color":1,"points":[0,0,1,1]}]}]}"""
        )
        assertFalse(scene.isVisible(scene.spaces.single().entities.single()))
    }
}
