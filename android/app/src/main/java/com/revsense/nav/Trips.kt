package com.revsense.nav

import android.content.Context
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** One trip from the board: "R|live|seq|durationS|distCentiMi|topMph|avgMph|maxRpm|maxCoolC|peakBoostDeciPsi|minBattDeciV|maxBattDeciV|codes". */
data class TripRec(
    val live: Boolean, val seq: Int, val durationS: Int, val distCentiMi: Int, val topMph: Int, val avgMph: Int, val maxRpm: Int,
    val maxCoolC: Int?, val peakBoostDeciPsi: Int?, val minBattDeciV: Int, val maxBattDeciV: Int, val codes: List<String>
)

object TripParser {
    private const val UNKNOWN = -32768
    fun parse(s: String): TripRec? {
        val p = s.trim().split('|')
        if (p.size < 13 || p[0] != "R") return null
        val n = (1..11).map { p[it].toIntOrNull() ?: return null }
        return TripRec(
            live = n[0] == 1, seq = n[1], durationS = n[2], distCentiMi = n[3], topMph = n[4], avgMph = n[5], maxRpm = n[6],
            maxCoolC = n[7].takeIf { it != UNKNOWN }, peakBoostDeciPsi = n[8].takeIf { it != UNKNOWN }, minBattDeciV = n[9], maxBattDeciV = n[10],
            codes = p[12].split(';').filter { it.isNotBlank() }
        )
    }
}

/** How the report rows read (same units as the board's own Drive report). */
object TripFmt {
    fun duration(sec: Int): String = when {
        sec < 60 -> "<1 min"
        sec < 3600 -> "${(sec + 30) / 60} min"
        else -> "${sec / 3600}h %02dm".format((sec % 3600 + 30) / 60 % 60)
    }
    fun distance(centiMi: Int, kmh: Boolean): String =
        if (kmh) "%.1f km".format(centiMi / 100.0 * 1.609344) else "%.1f mi".format(centiMi / 100.0)
    fun speed(mph: Int, kmh: Boolean): String = "${Fmt.speed(mph, kmh)} ${if (kmh) "km/h" else "mph"}"
    fun coolant(c: Int?, tempC: Boolean): String = if (c == null) "--" else "${Fmt.temp(c, tempC)} ${if (tempC) "°C" else "°F"}"
    fun boost(deciPsi: Int?, bar: Boolean): String = when {
        deciPsi == null -> "--"
        bar -> "%.2f bar".format(deciPsi / 10.0 * 0.0689476)
        else -> "%.1f psi".format(deciPsi / 10.0)
    }
    fun battery(minDeciV: Int, maxDeciV: Int): String =
        if (minDeciV <= 0 || maxDeciV <= 0) "--" else if (minDeciV == maxDeciV) "%.1f V".format(minDeciV / 10.0) else "%.1f – %.1f V".format(minDeciV / 10.0, maxDeciV / 10.0)
    fun codes(c: List<String>): String = if (c.isEmpty()) "none" else c.joinToString(", ")
}

/** The board has no clock, so the app remembers when it first saw each trip while it was running (seq -> time). */
object TripStore {
    private var prefs: android.content.SharedPreferences? = null
    private val seen = HashMap<Int, Long>()

    fun init(c: Context) {
        if (prefs != null) return
        prefs = c.applicationContext.getSharedPreferences("trips", Context.MODE_PRIVATE)
        prefs?.getString("seen", "")?.split(',')?.forEach { e -> val kv = e.split(':'); if (kv.size == 2) seen[kv[0].toIntOrNull() ?: return@forEach] = kv[1].toLongOrNull() ?: return@forEach }
    }

    @Synchronized fun note(seq: Int, live: Boolean) {
        if (!live || seen.containsKey(seq)) return
        seen[seq] = System.currentTimeMillis()
        val keep = seen.entries.sortedByDescending { it.key }.take(30)
        prefs?.edit()?.putString("seen", keep.joinToString(",") { "${it.key}:${it.value}" })?.apply()
    }

    @Synchronized fun whenSeen(seq: Int): String? = seen[seq]?.let { SimpleDateFormat("EEE h:mm a", Locale.getDefault()).format(Date(it)) }
}
