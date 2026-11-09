package com.revsense.nav

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

/**
 * Watches the Google Maps navigation notification and forwards each turn to the board over Bluetooth.
 * Android starts this service by itself once the user has allowed "Notification access" for the app.
 */
class NavListener : NotificationListenerService() {
    private var lastSent = ""
    private var lastSentAt = 0L

    override fun onListenerConnected() {
        AppLog.add("notification access is on")
        BleLink.start(this)
        activeNotifications?.forEach { handle(it) }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) = handle(sbn)

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (sbn.packageName != MAPS) return
        AppLog.add("Maps notification gone: route ended"); lastSent = ""
        AppState.turn = null
        BleLink.send("C")
    }

    private fun handle(sbn: StatusBarNotification) {
        if (sbn.packageName != MAPS) return
        BleLink.start(this)
        val e = sbn.notification.extras
        val fields = listOf(e.getCharSequence(Notification.EXTRA_TITLE), e.getCharSequence(Notification.EXTRA_TEXT),
                            e.getCharSequence(Notification.EXTRA_SUB_TEXT), e.getCharSequence(Notification.EXTRA_BIG_TEXT)).map { it?.toString() }
        val turn = MapsParser.parse(fields)
        if (turn == null) { AppLog.add("Maps (not a turn): " + fields.joinToString(" | ")); return }
        val msg = turn.message(); val now = System.currentTimeMillis()
        if (msg == lastSent && now - lastSentAt < 5000) return              // nothing changed (Maps re-posts often); repeat every 5 s so the board never times out
        AppState.turn = turn; AppState.turnAt = android.os.SystemClock.elapsedRealtime()
        val ok = BleLink.send(msg)
        AppLog.add((if (ok) "sent: " else "NOT sent (no link): ") + msg + "    [" + fields.joinToString(" | ") + "]")
        lastSent = msg; lastSentAt = now
    }

    companion object { const val MAPS = "com.google.android.apps.maps" }
}
