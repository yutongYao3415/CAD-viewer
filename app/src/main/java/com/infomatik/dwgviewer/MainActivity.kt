package com.infomatik.dwgviewer

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.Spinner
import android.widget.TextView
import java.io.File
import java.util.concurrent.Executors

class MainActivity : Activity() {
    private val worker = Executors.newSingleThreadExecutor()
    private lateinit var drawingView: DrawingView
    private lateinit var spacePicker: Spinner
    private lateinit var status: TextView
    private lateinit var progress: ProgressBar
    private var drawing: DrawingScene? = null
    private var displayName = "DWG 看图"
    private var loadSerial = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()
        val incoming = if (intent?.action == Intent.ACTION_VIEW) intent?.data else null
        val recent = getPreferences(MODE_PRIVATE).getString("recentUri", null)?.let(Uri::parse)
        (incoming ?: recent)?.let(::loadDrawing)
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(8, 6, 8, 6)
        }
        fun button(text: String, click: () -> Unit): Button = Button(this).apply {
            this.text = text
            setOnClickListener { click() }
            bar.addView(this, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
        button("打开") { openPicker() }
        button("适配") { drawingView.fitToScreen() }
        button("图层") { showLayers() }
        spacePicker = Spinner(this)
        bar.addView(spacePicker, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 2f))
        root.addView(bar)

        drawingView = DrawingView(this).apply {
            onSelection = { entity ->
                status.text = if (entity == null) displayName else
                    "${entity.type}  图层 ${entity.layer}  颜色 ${"#%06X".format(entity.color and 0xffffff)}  句柄 ${entity.handle}"
            }
        }
        val drawingFrame = FrameLayout(this)
        drawingFrame.addView(drawingView, FrameLayout.LayoutParams(-1, -1))
        progress = ProgressBar(this).apply { visibility = View.GONE }
        drawingFrame.addView(progress, FrameLayout.LayoutParams(-2, -2, Gravity.CENTER))
        root.addView(drawingFrame, LinearLayout.LayoutParams(-1, 0, 1f))
        status = TextView(this).apply {
            text = "打开手机中的 DWG 图纸"
            setPadding(16, 10, 16, 10)
        }
        root.addView(status)
        setContentView(root)
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            root.setOnApplyWindowInsetsListener { view, insets ->
                val bars = insets.getInsets(android.view.WindowInsets.Type.systemBars())
                view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
                insets
            }
        }
    }

    private fun openPicker() {
        val picker = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        }
        startActivityForResult(picker, OPEN_DWG)
    }

    @Deprecated("Framework document picker result")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != OPEN_DWG || resultCode != RESULT_OK) return
        val uri = data?.data ?: return
        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (_: SecurityException) {
            // A provider may grant access only for this launch.
        }
        loadDrawing(uri)
    }

    private fun loadDrawing(uri: Uri) {
        val serial = ++loadSerial
        progress.visibility = View.VISIBLE
        status.text = "正在打开图纸…"
        worker.execute {
            try {
                val name = documentName(uri)
                val copy = File(cacheDir, "viewer-current.dwg")
                contentResolver.openInputStream(uri)?.use { input ->
                    copy.outputStream().use { output -> input.copyTo(output) }
                } ?: throw IllegalArgumentException("无法读取所选文件")
                if (copy.length() == 0L) throw IllegalArgumentException("图纸文件为空")
                val scene = DrawingScene.fromJson(NativeBridge.readDrawing(copy.absolutePath))
                runOnUiThread {
                    if (serial != loadSerial) return@runOnUiThread
                    displayName = name
                    drawing = scene
                    drawingView.setDrawing(scene)
                    spacePicker.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item,
                        scene.spaces.map { it.name })
                    spacePicker.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
                        override fun onItemSelected(parent: android.widget.AdapterView<*>?, view: View?,
                                                    position: Int, id: Long) = drawingView.showSpace(position)
                        override fun onNothingSelected(parent: android.widget.AdapterView<*>?) = Unit
                    }
                    status.text = "$name · ${scene.spaces.size} 个空间 · ${scene.layers.size} 个图层"
                    if (contentResolver.persistedUriPermissions.any {
                            it.uri == uri && it.isReadPermission
                        }) {
                        getPreferences(MODE_PRIVATE).edit().putString("recentUri", uri.toString()).apply()
                    }
                    progress.visibility = View.GONE
                }
            } catch (error: Exception) {
                runOnUiThread {
                    if (serial != loadSerial) return@runOnUiThread
                    progress.visibility = View.GONE
                    status.text = "打开失败"
                    AlertDialog.Builder(this).setTitle("无法打开图纸")
                        .setMessage(error.message ?: "文件读取失败")
                        .setPositiveButton("确定", null).show()
                }
            }
        }
    }

    private fun documentName(uri: Uri): String {
        var cursor: Cursor? = null
        try {
            cursor = contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            if (cursor?.moveToFirst() == true) return cursor.getString(0) ?: "图纸.dwg"
        } finally {
            cursor?.close()
        }
        return uri.lastPathSegment ?: "图纸.dwg"
    }

    private fun showLayers() {
        val layers = drawing?.layers ?: return
        AlertDialog.Builder(this).setTitle("图层")
            .setMultiChoiceItems(layers.map { it.name }.toTypedArray(),
                layers.map { it.visible }.toBooleanArray()) { _, which, checked ->
                layers[which].visible = checked
                drawingView.refreshLayers()
            }
            .setPositiveButton("完成", null).show()
    }

    override fun onDestroy() {
        loadSerial++
        worker.shutdown()
        super.onDestroy()
    }

    companion object { private const val OPEN_DWG = 200 }
}
