package com.revsense.nav

import org.junit.Assert.*
import org.junit.Test

class LiveParserTest {
    @Test fun parsesLine() {
        val l = LiveParser.parse("L|2150|45|92|140|61|42|3")!!
        assertEquals(2150, l.rpm); assertEquals(45, l.mph); assertEquals(92, l.coolC); assertEquals(140, l.battDeciV); assertEquals(61, l.fuel); assertEquals(42, l.load)
        assertTrue(l.kmh); assertTrue(l.tempC)
    }
    @Test fun unknownValuesAreMinusOne() { val l = LiveParser.parse("L|-1|-1|-1|-1|-1|-1|0")!!; assertEquals(-1, l.rpm); assertFalse(l.kmh); assertFalse(l.tempC) }
    @Test fun garbageIsNull() { assertNull(LiveParser.parse("")); assertNull(LiveParser.parse("N|R|3|x")); assertNull(LiveParser.parse("L|1|2")); assertNull(LiveParser.parse("L|a|b|c|d|e|f|g")) }
    @Test fun distances() {
        assertEquals("300" to "ft", Fmt.dist(91, false)); assertEquals("0.3" to "mi", Fmt.dist(483, false)); assertEquals("350" to "m", Fmt.dist(347, true)); assertEquals("1.2" to "km", Fmt.dist(1200, true))
        assertEquals("10" to "mi", Fmt.dist(16093, false))
    }
    @Test fun conversions() { assertEquals(72, Fmt.speed(45, true)); assertEquals(45, Fmt.speed(45, false)); assertEquals(198, Fmt.temp(92, false)); assertEquals(92, Fmt.temp(92, true)) }
}
