package com.revsense.nav

import org.junit.Assert.*
import org.junit.Test

class CodesTest {
    @Test fun assemblesAnAnswer() {
        val a = CodesAssembler()
        assertNull(a.feed("M|1|1|2|2|UPDATED: 2 CODES")); assertNull(a.feed("D|P0301|0|1|Misfire cylinder 1")); assertNull(a.feed("D|P0420|1|0|Catalyst weak B1"))
        val s = a.feed("E|2")!!
        assertTrue(s.answered); assertTrue(s.mil); assertEquals(2, s.total); assertEquals("UPDATED: 2 CODES", s.status)
        assertEquals("P0301", s.rows[0].code); assertEquals("NEW", s.rows[0].tag); assertEquals("PENDING", s.rows[1].tag); assertEquals("Catalyst weak B1", s.rows[1].desc)
    }
    @Test fun noCodesAndStoredTag() {
        val a = CodesAssembler(); a.feed("M|1|0|0|0|NO CODES"); val s = a.feed("E|0")!!
        assertEquals(0, s.rows.size); assertFalse(s.mil)
        a.feed("M|1|0|1|1|"); a.feed("D|P0171|0|0|"); assertEquals("STORED", a.feed("E|1")!!.rows[0].tag)
    }
    @Test fun garbageIsIgnored() { val a = CodesAssembler(); assertNull(a.feed("")); assertNull(a.feed("E|1")); assertNull(a.feed("D|x")); assertNull(a.feed("M|1")) }
}
