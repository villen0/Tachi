package com.revsense.nav

import android.Manifest
import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.view.ViewGroup
import android.widget.*

/** One screen: two permission buttons, the link state and a log of what Maps said and what was sent to the board. */
class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var log: TextView

    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad * 2, pad, pad) }
        fun btn(t: String, f: () -> Unit) = Button(this).apply { text = t; setOnClickListener { f() } }.also { col.addView(it) }
        status = TextView(this).apply { textSize = 18f }.also { col.addView(it) }
        btn("1. Allow Bluetooth") { askBluetooth() }
        btn("2. Allow notification access (find Revsense Nav)") { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) }
        btn("Send a test turn to the board") { val ok = BleLink.send("N|R|300|Test St"); AppLog.add(if (ok) "sent test turn" else "no link to the board") }
        btn("Connect now") { BleLink.start(this); NotificationListenerService.requestRebind(ComponentName(this, NavListener::class.java)) }
        log = TextView(this).apply { textSize = 12f; typeface = android.graphics.Typeface.MONOSPACE }
        col.addView(ScrollView(this).apply { addView(log) }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(col)
        AppLog.onChange = { refresh() }
    }

    override fun onResume() { super.onResume(); if (BleLink.hasPermission(this)) BleLink.start(this); refresh() }
    override fun onDestroy() { AppLog.onChange = null; super.onDestroy() }

    private fun askBluetooth() {
        val p = if (Build.VERSION.SDK_INT >= 31) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT) else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        requestPermissions(p, 1)
    }
    override fun onRequestPermissionsResult(code: Int, p: Array<out String>, r: IntArray) { if (BleLink.hasPermission(this)) BleLink.start(this); refresh() }

    private fun listenerOn(): Boolean = Settings.Secure.getString(contentResolver, "enabled_notification_listeners")?.contains(packageName) == true
    private fun refresh() {
        status.text = "Board: ${AppLog.bleState}\nNotification access: " + if (listenerOn()) "on" else "OFF (step 2)"
        log.text = AppLog.text()
    }
}
