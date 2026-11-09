package com.revsense.nav

import org.junit.Assert.*
import org.junit.Test

class ColorMathTest {
    @Test fun hsv() {
        assertEquals(0xFF0000, ColorMath.hsvToRgb(0f, 1f, 1f)); assertEquals(0x00FF00, ColorMath.hsvToRgb(120f, 1f, 1f)); assertEquals(0x0000FF, ColorMath.hsvToRgb(240f, 1f, 1f))
        assertEquals(0xFFFFFF, ColorMath.hsvToRgb(77f, 0f, 1f)); assertEquals(0x000000, ColorMath.hsvToRgb(10f, 1f, 0f)); assertEquals(0xFFFF00, ColorMath.hsvToRgb(60f, 1f, 1f))
    }
    @Test fun hsvRoundTrip() {
        for (c in listOf(0xE7462A, 0x00C8FF, 0x6EF082, 0xFFD032, 0x808080, 0x123456)) {
            val h = ColorMath.rgbToHsv(c); val back = ColorMath.hsvToRgb(h[0], h[1], h[2])
            for (sh in listOf(16, 8, 0)) assertTrue("$c vs $back", Math.abs((c shr sh and 255) - (back shr sh and 255)) <= 1)
        }
    }
    @Test fun hsl() { assertEquals(0xFF0000, ColorMath.hslToRgb(0f, 1f, 0.5f)); assertEquals(0xFFFFFF, ColorMath.hslToRgb(0f, 1f, 1f)); assertEquals(0x000000, ColorMath.hslToRgb(0f, 1f, 0f)) }
    @Test fun cells() {
        assertNull(ColorMath.cellAt(0.05f, 10f)); assertEquals(0 to 0, ColorMath.cellAt(0.2f, 5f)); assertEquals(5 to 23, ColorMath.cellAt(0.99f, 359f)); assertEquals(5 to 6, ColorMath.cellAt(1f, 100f))
        assertEquals(0 to 0, ColorMath.cellAt(0.2f, 365f)); assertEquals(2 to 0, ColorMath.nearestCell(ColorMath.cellColor(2, 0))); assertEquals(4 to 12, ColorMath.nearestCell(ColorMath.cellColor(4, 12)))
    }
}
