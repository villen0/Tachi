package com.revsense.nav

import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/** Colour maths for the two wheels (pure Kotlin, tested). Colours are 0xRRGGBB ints. */
object ColorMath {
    const val RINGS = 6; const val SEGS = 24; const val HOLE = 0.14f      // the gradient wheel: 6 rings x 24 hues around a small white hub

    private fun pack(r: Float, g: Float, b: Float): Int = ((r * 255f + 0.5f).toInt() shl 16) or ((g * 255f + 0.5f).toInt() shl 8) or (b * 255f + 0.5f).toInt()

    private fun fromChroma(h: Float, c: Float, m: Float): Int {
        val hh = ((h % 360f) + 360f) % 360f / 60f
        val x = c * (1f - abs(hh % 2f - 1f))
        val (r, g, b) = when (hh.toInt()) { 0 -> Triple(c, x, 0f); 1 -> Triple(x, c, 0f); 2 -> Triple(0f, c, x); 3 -> Triple(0f, x, c); 4 -> Triple(x, 0f, c); else -> Triple(c, 0f, x) }
        return pack(r + m, g + m, b + m)
    }

    /** h in degrees, s and v in 0..1. */
    fun hsvToRgb(h: Float, s: Float, v: Float): Int { val c = v * s; return fromChroma(h, c, v - c) }
    fun hslToRgb(h: Float, s: Float, l: Float): Int { val c = (1f - abs(2f * l - 1f)) * s; return fromChroma(h, c, l - c / 2f) }

    /** Returns h (0..360), s, v. */
    fun rgbToHsv(rgb: Int): FloatArray {
        val r = (rgb shr 16 and 255) / 255f; val g = (rgb shr 8 and 255) / 255f; val b = (rgb and 255) / 255f
        val mx = max(r, max(g, b)); val mn = min(r, min(g, b)); val d = mx - mn
        val h = when { d == 0f -> 0f; mx == r -> 60f * (((g - b) / d) % 6f); mx == g -> 60f * ((b - r) / d + 2f); else -> 60f * ((r - g) / d + 4f) }
        return floatArrayOf(((h % 360f) + 360f) % 360f, if (mx == 0f) 0f else d / mx, mx)
    }

    /** The gradient wheel's cell colour: hue every 15 degrees clockwise from 12 o'clock, dark rings inside, light outside. */
    fun cellColor(ring: Int, seg: Int): Int = hslToRgb(seg * 360f / SEGS, 1f, 0.2f + 0.11f * ring)

    /** The cell under a touch: r = distance from the centre as a fraction of the radius, angleCw = degrees clockwise from 12 o'clock. Null in the hub. */
    fun cellAt(r: Float, angleCw: Float): Pair<Int, Int>? {
        if (r < HOLE) return null
        val ring = min(RINGS - 1, floor((r - HOLE) / ((1f - HOLE) / RINGS)).toInt())
        val seg = floor((((angleCw % 360f) + 360f) % 360f) / (360f / SEGS)).toInt() % SEGS
        return ring to seg
    }

    /** The cell whose colour is closest to rgb (to show where the current colour sits). */
    fun nearestCell(rgb: Int): Pair<Int, Int> {
        var best = 0 to 0; var bd = Int.MAX_VALUE
        for (ring in 0 until RINGS) for (seg in 0 until SEGS) {
            val c = cellColor(ring, seg)
            val dr = (c shr 16 and 255) - (rgb shr 16 and 255); val dg = (c shr 8 and 255) - (rgb shr 8 and 255); val db = (c and 255) - (rgb and 255)
            val d = dr * dr + dg * dg + db * db; if (d < bd) { bd = d; best = ring to seg }
        }
        return best
    }
}
