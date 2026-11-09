package com.revsense.nav

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.sin

private fun argb(rgb: Int) = Color(0xFF000000.toInt() or (rgb and 0xFFFFFF))

/** Calls onPoint for every touch position while a finger is down (final = false) and once more when it lifts (final = true). */
private suspend fun PointerInputScope.wheelGestures(onPoint: (Offset, Boolean) -> Unit) {
    awaitEachGesture {
        val down = awaitFirstDown(); var last = down.position; down.consume(); onPoint(last, false)
        while (true) {
            val ev = awaitPointerEvent(); val ch = ev.changes.firstOrNull() ?: break
            last = ch.position; ch.consume()
            if (!ch.pressed) break
            onPoint(last, false)
        }
        onPoint(last, true)
    }
}

/** "Standard colorwheel": hue around, white in the middle (saturation grows outward), full brightness. */
@Composable
fun StandardWheel(rgb: Int, onPick: (Int, Boolean) -> Unit) {
    val hsv = ColorMath.rgbToHsv(rgb)
    val stops = remember { (0..12).map { i -> argb(ColorMath.hsvToRgb(270f - i * 30f, 1f, 1f)) } }          // sweepGradient starts at 3 o'clock, clockwise; red sits at the top and hue runs counter-clockwise
    Canvas(Modifier.fillMaxWidth().aspectRatio(1f).pointerInput(Unit) {
        wheelGestures { p, fin ->
            val c = Offset(size.width / 2f, size.height / 2f); val r = min(size.width, size.height) / 2f
            val dx = p.x - c.x; val dy = p.y - c.y
            val angleCw = Math.toDegrees(atan2(dx.toDouble(), -dy.toDouble())).toFloat()
            val sat = min(1f, hypot(dx, dy) / r)
            onPick(ColorMath.hsvToRgb(((360f - angleCw) % 360f + 360f) % 360f, sat, 1f), fin)
        }
    }) {
        val r = size.minDimension / 2f; val c = center
        drawCircle(Brush.sweepGradient(stops, c), r, c)
        drawCircle(Brush.radialGradient(listOf(Color.White, Color.Transparent), c, r), r, c)
        val a = Math.toRadians(((360f - hsv[0]) % 360f).toDouble()); val pos = Offset(c.x + (sin(a) * hsv[1] * r).toFloat(), c.y - (cos(a) * hsv[1] * r).toFloat())
        drawCircle(argb(rgb), 11.dp.toPx(), pos)
        drawCircle(Color.White, 12.dp.toPx(), pos, style = Stroke(3.dp.toPx()))
    }
}

/** "Gradient colorwheel": 24 hues x 6 shades (dark inside, light outside) and a white hub. */
@Composable
fun GradientWheel(rgb: Int, onPick: (Int, Boolean) -> Unit) {
    val sel = remember(rgb) { ColorMath.nearestCell(rgb) }
    var picked by remember(rgb) { mutableStateOf<Pair<Int, Int>?>(null) }
    val mark = picked ?: sel
    Canvas(Modifier.fillMaxWidth().aspectRatio(1f).pointerInput(Unit) {
        wheelGestures { p, fin ->
            val c = Offset(size.width / 2f, size.height / 2f); val r = min(size.width, size.height) / 2f
            val dx = p.x - c.x; val dy = p.y - c.y
            val cell = ColorMath.cellAt(min(1f, hypot(dx, dy) / r), Math.toDegrees(atan2(dx.toDouble(), -dy.toDouble())).toFloat())
            if (cell == null) { picked = null; onPick(0xFFFFFF, fin) } else { picked = cell; onPick(ColorMath.cellColor(cell.first, cell.second), fin) }
        }
    }) {
        val r = size.minDimension / 2f; val c = center; val ringW = r * (1f - ColorMath.HOLE) / ColorMath.RINGS
        for (ring in 0 until ColorMath.RINGS) for (seg in 0 until ColorMath.SEGS) {
            val rm = r * ColorMath.HOLE + ringW * (ring + 0.5f)
            drawArc(argb(ColorMath.cellColor(ring, seg)), -90f + seg * 15f - 0.3f, 15.6f, false, Offset(c.x - rm, c.y - rm), Size(2 * rm, 2 * rm), style = Stroke(ringW + 0.6f))
        }
        drawCircle(Color.White, r * ColorMath.HOLE * 0.85f, c)
        val (mr, ms) = mark; val a = Math.toRadians((ms * 15f + 7.5f).toDouble()); val rr = r * ColorMath.HOLE + ringW * (mr + 0.5f)
        val pos = Offset(c.x + (sin(a) * rr).toFloat(), c.y - (cos(a) * rr).toFloat())
        drawCircle(Color.White, 11.dp.toPx(), pos, style = Stroke(3.dp.toPx()))
    }
}
