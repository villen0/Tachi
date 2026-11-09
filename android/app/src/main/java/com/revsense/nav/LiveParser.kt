package com.revsense.nav

/** The live values the board streams: "L|rpm|mph|coolC|battDeciV|fuelPct|loadPct|units" (-1 = no fresh value; units bits: 1 km/h, 2 deg C, 4 bar). */
data class Live(val rpm: Int, val mph: Int, val coolC: Int, val battDeciV: Int, val fuel: Int, val load: Int, val units: Int) {
    val kmh: Boolean get() = units and 1 != 0
    val tempC: Boolean get() = units and 2 != 0
}

object LiveParser {
    fun parse(s: String): Live? {
        val p = s.trim().split('|')
        if (p.size < 8 || p[0] != "L") return null
        val n = p.drop(1).map { it.toIntOrNull() ?: return null }
        return Live(n[0], n[1], n[2], n[3], n[4], n[5], n[6])
    }
}

/** Formatting shared by the screens (pure, tested). */
object Fmt {
    /** Distance as the board shows it: feet / miles or metres / km. Returns number and unit. */
    fun dist(m: Int, metric: Boolean): Pair<String, String> {
        if (metric) {
            return if (m < 1000) ((m + 5) / 10 * 10).toString() to "m" else (if (m / 1000.0 < 9.95) "%.1f".format(m / 1000.0) else "%.0f".format(m / 1000.0)) to "km"
        }
        val ft = m * 3.28084
        if (ft < 528.0) return ((ft / 50.0 + 0.5).toInt() * 50).toString() to "ft"
        val mi = m / 1609.344
        return (if (mi < 9.95) "%.1f".format(mi) else "%.0f".format(mi)) to "mi"
    }
    fun speed(mph: Int, kmh: Boolean): Int = if (kmh) Math.round(mph * 1.609344).toInt() else mph
    fun temp(c: Int, tempC: Boolean): Int = if (tempC) c else Math.round(c * 9.0 / 5.0 + 32).toInt()
    fun closeness(m: Int): Float = (1f - m / 500f).coerceIn(0f, 1f)
}
