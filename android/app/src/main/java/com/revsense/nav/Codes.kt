package com.revsense.nav

/** The board's fault codes: "M|answered|mil|milCount|total|status", then "D|code|kind|isNew|description" per code, then "E|count". */
data class CodeRow(val code: String, val kind: Int, val isNew: Boolean, val desc: String) {
    val tag: String get() = if (isNew) "NEW" else if (kind == 1) "PENDING" else "STORED"
}
data class CodesState(val answered: Boolean, val mil: Boolean, val milCount: Int, val total: Int, val status: String, val rows: List<CodeRow>)

/** Collects the lines of one answer; returns the finished state at the "E" line. */
class CodesAssembler {
    private var header: List<String>? = null
    private val rows = ArrayList<CodeRow>()

    fun feed(line: String): CodesState? {
        val p = line.trim().split('|')
        when (p.firstOrNull()) {
            "M" -> { if (p.size < 5) return null; header = p; rows.clear() }
            "D" -> { if (p.size < 4) return null; rows.add(CodeRow(p[1], p[2].toIntOrNull() ?: 0, p[3] == "1", p.drop(4).joinToString("|"))) }
            "E" -> {
                val h = header ?: return null
                val s = CodesState(h[1] == "1", h[2] == "1", h[3].toIntOrNull() ?: -1, h[4].toIntOrNull() ?: rows.size, h.drop(5).joinToString("|"), rows.toList())
                header = null; rows.clear(); return s
            }
        }
        return null
    }
}
