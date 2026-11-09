package com.revsense.nav

/** One turn, ready to send to the board: code is S L R SL SR HL HR UL UR RB A (see src/nav.h in the firmware). */
data class Turn(val code: String, val meters: Int, val street: String) {
    fun message(): String = "N|$code|$meters|${street.replace('|', ' ').take(60)}"
}

/**
 * Turns the text of the Google Maps navigation notification into a [Turn]. Pure Kotlin so it is unit-tested on a PC.
 * Maps changes its notification layout now and then, so this is forgiving: it looks at the title, text and sub text together,
 * finds a distance ("0.3 mi", "300 ft", "450 m", "1.2 km") and a manoeuvre word ("turn left", "roundabout", ...), and takes the street
 * from what is left. The app's log shows the raw texts, so a change in Maps is easy to spot.
 */
object MapsParser {
    private val distRe = Regex("""(\d+(?:[.,]\d+)?)\s*(mi|miles?|ft|feet|yd|yards?|km|m)\b""", RegexOption.IGNORE_CASE)
    private val instructionRe = Regex("""^(turn|continue|head|merge|keep|bear|take|slight|sharp|make|at the|your|you|arrive|destination|exit|stay|follow|use|enter)\b""", RegexOption.IGNORE_CASE)
    private val etaRe = Regex("""\b\d+\s*(min|h|hr)\b|\b\d{1,2}:\d{2}\b|\barriv""", RegexOption.IGNORE_CASE)

    fun meters(value: Double, unit: String): Int = when (unit.lowercase()) {
        "mi", "mile", "miles" -> value * 1609.344
        "ft", "feet" -> value * 0.3048
        "yd", "yard", "yards" -> value * 0.9144
        "km" -> value * 1000.0
        else -> value
    }.toInt()

    fun direction(text: String): String? {
        val t = text.lowercase()
        val left = "left" in t; val right = "right" in t
        return when {
            "u-turn" in t || "u turn" in t -> if (right) "UR" else "UL"
            "roundabout" in t || "traffic circle" in t || "rotary" in t -> "RB"
            "destination" in t || "you have arrived" in t || t.startsWith("arrive") -> "A"
            "sharp" in t && left -> "HL"
            "sharp" in t && right -> "HR"
            ("slight" in t || "bear" in t || "keep" in t || "fork" in t || "ramp" in t || "exit" in t) && left -> "SL"
            ("slight" in t || "bear" in t || "keep" in t || "fork" in t || "ramp" in t || "exit" in t) && right -> "SR"
            left -> "L"
            right -> "R"
            "continue" in t || "straight" in t || "head" in t || "merge" in t || "follow" in t -> "S"
            else -> null
        }
    }

    /** @param fields title, text, sub text, big text of the notification (null or blank ones are skipped) */
    fun parse(fields: List<String?>): Turn? {
        val f = fields.filterNotNull().map { it.trim() }.filter { it.isNotEmpty() }
        if (f.isEmpty()) return null
        val joined = f.joinToString(" | ")
        val m = distRe.find(joined)
        val meters = if (m != null) meters(m.groupValues[1].replace(',', '.').toDouble(), m.groupValues[2]) else 0
        val dir = direction(joined) ?: return null
        var street = ""
        for (s in f) {
            if (etaRe.containsMatchIn(s) && !s.contains(" onto ", true)) continue          // "12 min · 4.2 mi · 10:35 AM" is the trip summary, not the street
            var x = distRe.replace(s, "").trim(' ', '·', '-', '–', '—', ',', '|', '•')
            var cut = false
            for (k in listOf(" onto ", " toward ", " towards ", " to stay on ", " on ")) {
                val i = x.lowercase().indexOf(k); if (i >= 0) { x = x.substring(i + k.length); cut = true; break }
            }
            if (!cut && instructionRe.containsMatchIn(x)) continue                           // an instruction without a street ("Keep right to take the ramp")
            if (cut && x.lowercase().startsWith("the ")) continue                            // "...is on the right"
            if (x.isNotEmpty() && x.length <= 80) { street = x; break }
        }
        return Turn(dir, meters, street)
    }
}
