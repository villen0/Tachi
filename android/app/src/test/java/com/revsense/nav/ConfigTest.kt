package com.revsense.nav

import org.junit.Assert.*
import org.junit.Test

class ConfigTest {
    @Test fun parsesConfig() {
        val c = ConfigParser.parse("C|0|1|0|4|0|1|100|108|120|15|6500|85")!!
        assertFalse(c.speedKmh); assertTrue(c.tempC); assertFalse(c.pressBar); assertEquals(4, c.theme); assertEquals(0, c.mode); assertEquals(1, c.gauge)
        assertEquals(100, c.coolWarnC); assertEquals(108, c.coolHotC); assertEquals(120, c.battLowDeciV); assertEquals(15, c.fuelLowPct); assertEquals(6500, c.redlineRpm); assertEquals(85, c.brightness)
    }
    @Test fun customColour() {
        assertEquals(0xE7462A, ConfigParser.parse("C|0|1|0|4|7|1|100|108|120|15|6500|85|15156778")!!.customRgb)
        assertEquals(0xE7462A, ConfigParser.parse("C|0|1|0|4|0|1|100|108|120|15|6500|85")!!.customRgb)      // older firmware: no colour sent
    }
    @Test fun pages() {
        val p = PagesParser.parse("G|2|1|Tach,Speedo,Boost,Settings")!!
        assertEquals(2, p.current); assertTrue(p.screenOn); assertEquals(listOf("Tach", "Speedo", "Boost", "Settings"), p.names)
        assertFalse(PagesParser.parse("G|0|0|Tach")!!.screenOn); assertNull(PagesParser.parse("G|x|1|a")); assertNull(PagesParser.parse("C|1|2")); assertNull(PagesParser.parse("G|1"))
    }
    @Test fun garbage() { assertNull(ConfigParser.parse("")); assertNull(ConfigParser.parse("C|1|2")); assertNull(ConfigParser.parse("L|0|1|0|4|0|1|100|108|120|15|6500|85")); assertNull(ConfigParser.parse("C|a|1|0|4|0|1|100|108|120|15|6500|85")) }
    @Test fun namesMatchTheBoard() { assertEquals(5, Names.looks.size); assertEquals(5, Names.looksRgb.size); assertEquals(8, Names.modes.size); assertEquals(8, Names.modesRgb.size); assertEquals(3, Names.gauges.size) }
}
