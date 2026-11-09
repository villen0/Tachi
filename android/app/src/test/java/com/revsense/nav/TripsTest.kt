package com.revsense.nav

import org.junit.Assert.*
import org.junit.Test

class TripsTest {
    @Test fun parsesATrip() {
        val t = TripParser.parse("R|1|12|1980|1120|74|31|4850|95|-24|131|142|P0301;P0420")!!
        assertTrue(t.live); assertEquals(12, t.seq); assertEquals(1980, t.durationS); assertEquals(1120, t.distCentiMi); assertEquals(74, t.topMph)
        assertEquals(95, t.maxCoolC); assertEquals(-24, t.peakBoostDeciPsi); assertEquals(listOf("P0301", "P0420"), t.codes)
    }
    @Test fun unknownsAndNoCodes() {
        val t = TripParser.parse("R|0|3|100|20|40|20|3000|-32768|-32768|0|0|")!!
        assertFalse(t.live); assertNull(t.maxCoolC); assertNull(t.peakBoostDeciPsi); assertTrue(t.codes.isEmpty())
        assertEquals("--", TripFmt.coolant(t.maxCoolC, false)); assertEquals("--", TripFmt.boost(t.peakBoostDeciPsi, false)); assertEquals("--", TripFmt.battery(0, 0)); assertEquals("none", TripFmt.codes(t.codes))
    }
    @Test fun garbage() { assertNull(TripParser.parse("")); assertNull(TripParser.parse("E|3")); assertNull(TripParser.parse("R|1|2|3")); assertNull(TripParser.parse("R|a|b|c|d|e|f|g|h|i|j|k|l")) }
    @Test fun formats() {
        assertEquals("33 min", TripFmt.duration(1980)); assertEquals("1h 05m", TripFmt.duration(3900)); assertEquals("<1 min", TripFmt.duration(40))
        assertEquals("11.2 mi", TripFmt.distance(1120, false)); assertEquals("18.0 km", TripFmt.distance(1120, true))
        assertEquals("74 mph", TripFmt.speed(74, false)); assertEquals("119 km/h", TripFmt.speed(74, true))
        assertEquals("203 °F", TripFmt.coolant(95, false)); assertEquals("95 °C", TripFmt.coolant(95, true))
        assertEquals("-2.4 psi", TripFmt.boost(-24, false)); assertEquals("-0.17 bar", TripFmt.boost(-24, true))
        assertEquals("13.1 – 14.2 V", TripFmt.battery(131, 142)); assertEquals("14.0 V", TripFmt.battery(140, 140))
    }
}
