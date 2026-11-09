package com.revsense.nav

import android.os.Handler
import android.os.Looper

/** A tiny in-memory log the screen shows: what Maps wrote, what was sent to the board, the Bluetooth state. */
object AppLog {
    private val lines = ArrayDeque<String>()
    private val main = Handler(Looper.getMainLooper())
    @Volatile var bleState: String = "not started"
    var onChange: (() -> Unit)? = null

    @Synchronized fun add(s: String) {
        lines.addFirst(s); while (lines.size > 40) lines.removeLast()
        main.post { onChange?.invoke() }
    }
    fun setBle(s: String) { bleState = s; AppState.bleText = s; AppState.bleConnected = s.startsWith("connected"); add("Bluetooth: $s") }
    @Synchronized fun text(): String = lines.joinToString("\n")
}
