package com.infomatik.dwgviewer

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class DwgReadTest {
    @Test fun readsSixDwgVersionsOffline() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        for (version in listOf("2000", "2004", "2007", "2010", "2013", "2018")) {
            val file = File.createTempFile("dwg-$version-", ".dwg", instrumentation.targetContext.cacheDir)
            try {
                instrumentation.context.assets.open("example_$version.dwg").use { input ->
                    file.outputStream().use { input.copyTo(it) }
                }
                val scene = DrawingScene.fromJson(NativeBridge.readDrawing(file.absolutePath))
                assertEquals("模型", scene.spaces.first().name)
                assertTrue("$version has layers", scene.layers.isNotEmpty())
                assertTrue("$version has drawable entities", scene.spaces.any { it.entities.isNotEmpty() })
            } finally {
                file.delete()
            }
        }
    }

    @Test fun damagedDwgShowsError() {
        val file = File.createTempFile("damaged-", ".dwg",
            InstrumentationRegistry.getInstrumentation().targetContext.cacheDir)
        try {
            file.writeText("invalid DWG content")
            val result = NativeBridge.readDrawing(file.absolutePath)
            assertTrue(result.contains("error"))
        } finally {
            file.delete()
        }
    }
}
