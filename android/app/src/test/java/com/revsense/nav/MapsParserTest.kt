package com.revsense.nav

import org.junit.Assert.*
import org.junit.Test

class MapsParserTest {
    @Test fun turnLeftOntoStreet() {
        val t = MapsParser.parse(listOf("0.3 mi", "Turn left onto Main St", "12 min · 4.2 mi · 10:35 AM"))!!
        assertEquals("L", t.code); assertEquals(482, t.meters); assertEquals("Main St", t.street)
    }
    @Test fun feetAndRight() {
        val t = MapsParser.parse(listOf("300 ft", "Turn right onto Broad St", null))!!
        assertEquals("R", t.code); assertEquals(91, t.meters); assertEquals("Broad St", t.street)
    }
    @Test fun distanceInTitleWithStreet() {
        val t = MapsParser.parse(listOf("500 m · Oak Ave", "Slight left", ""))!!
        assertEquals("SL", t.code); assertEquals(500, t.meters); assertEquals("Oak Ave", t.street)
    }
    @Test fun roundaboutAndUturnAndArrive() {
        assertEquals("RB", MapsParser.parse(listOf("0.2 mi", "At the roundabout, take the 2nd exit onto Park Rd"))!!.code)
        assertEquals("UL", MapsParser.parse(listOf("100 ft", "Make a U-turn"))!!.code)
        assertEquals("UR", MapsParser.parse(listOf("100 ft", "Make a right U-turn"))!!.code)
        assertEquals("A", MapsParser.parse(listOf("50 ft", "Your destination is on the right"))!!.code)
    }
    @Test fun straightAndKeep() {
        assertEquals("S", MapsParser.parse(listOf("2.1 mi", "Continue on Highway 9"))!!.code)
        assertEquals("SR", MapsParser.parse(listOf("0.5 mi", "Keep right to take the ramp"))!!.code)
        assertEquals("", MapsParser.parse(listOf("0.5 mi", "Keep right to take the ramp"))!!.street)
        assertEquals("", MapsParser.parse(listOf("50 ft", "Your destination is on the right"))!!.street)
        assertEquals("HR", MapsParser.parse(listOf("200 ft", "Sharp right onto 5th St"))!!.code)
    }
    @Test fun nothingUsefulGivesNull() {
        assertNull(MapsParser.parse(listOf("Google Maps", "")))
        assertNull(MapsParser.parse(emptyList()))
    }
    @Test fun messageFormat() {
        assertEquals("N|R|91|Broad St", Turn("R", 91, "Broad St").message())
        assertEquals("N|S|0|", Turn("S", 0, "").message())
    }
}
