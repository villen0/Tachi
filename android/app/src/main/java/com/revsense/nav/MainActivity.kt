package com.revsense.nav

import android.Manifest
import android.content.ComponentName
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.service.notification.NotificationListenerService
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent

class MainActivity : ComponentActivity() {
    override fun onCreate(b: Bundle?) {
        super.onCreate(b)
        setContent {
            RevsenseTheme {
                AppRoot(askBluetooth = { askBluetooth() }, askNotification = { startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)) })
            }
        }
    }

    override fun onResume() {
        super.onResume()
        refreshState()
        if (BleLink.hasPermission(this)) BleLink.start(this)
        if (AppState.listenerOn) NotificationListenerService.requestRebind(ComponentName(this, NavListener::class.java))
    }

    private fun askBluetooth() {
        val p = if (Build.VERSION.SDK_INT >= 31) arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT) else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        requestPermissions(p, 1)
    }

    override fun onRequestPermissionsResult(code: Int, p: Array<out String>, r: IntArray) {
        super.onRequestPermissionsResult(code, p, r)
        refreshState(); if (BleLink.hasPermission(this)) BleLink.start(this)
    }

    private fun refreshState() {
        AppState.btPermission = BleLink.hasPermission(this)
        AppState.listenerOn = Settings.Secure.getString(contentResolver, "enabled_notification_listeners")?.contains(packageName) == true
    }
}
