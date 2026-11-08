package com.infomatik.dwgviewer

object NativeBridge {
    init { System.loadLibrary("dwgviewer") }
    external fun readDrawing(path: String): String
}
