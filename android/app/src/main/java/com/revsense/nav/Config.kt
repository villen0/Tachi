package com.revsense.nav

/** The board's settings: "C|speedKmh|tempC|pressBar|theme|mode|gauge|coolWarnC|coolHotC|battLowDeciV|fuelLowPct|redlineRpm|brightness". */
data class BoardConfig(
    val speedKmh: Boolean, val tempC: Boolean, val pressBar: Boolean, val theme: Int, val mode: Int, val gauge: Int,
    val coolWarnC: Int, val coolHotC: Int, val battLowDeciV: Int, val fuelLowPct: Int, val redlineRpm: Int, val brightness: Int
)

object ConfigParser {
    fun parse(s: String): BoardConfig? {
        val p = s.trim().split('|')
        if (p.size < 13 || p[0] != "C") return null
        val n = (1..12).map { p[it].toIntOrNull() ?: return null }
        return BoardConfig(n[0] != 0, n[1] != 0, n[2] != 0, n[3], n[4], n[5], n[6], n[7], n[8], n[9], n[10], n[11])
    }
}

/** Names and colours of the choices (same order as the board's theme.h). */
object Names {
    val looks = listOf("Classic", "Sport Red", "Ice Silver", "Track Orange", "Revsense")
    val looksRgb = listOf(0xFF00C8FF, 0xFFFF463C, 0xFFBED7FF, 0xFFFF961E, 0xFFE7462A)
    val modes = listOf("Theme", "Cyan", "Green", "Amber", "Orange", "Red", "Violet")
    val modesRgb = listOf(0xFF808AA0, 0xFF00C8FF, 0xFF6EF082, 0xFFFFD032, 0xFFFF7828, 0xFFFF465A, 0xFFC864FF)
    val gauges = listOf("Dial", "Arc", "Ticks")
}
