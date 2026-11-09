package com.revsense.nav

import android.os.SystemClock
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.delay

// Revsense look: near-black background, dark cards, the red accent of the board's REVSENSE theme.
object Rv {
    val Bg = Color(0xFF0B0D12); val Card = Color(0xFF161A23); val Edge = Color(0xFF262C3A); val Track = Color(0xFF1E232E)
    val Text = Color(0xFFF0F3FA); val Dim = Color(0xFF808AA0); val Acc = Color(0xFFE7462A); val Ok = Color(0xFF46C878); val Warn = Color(0xFFFFBE3C)
}

@Composable
fun RevsenseTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = darkColorScheme(primary = Rv.Acc, background = Rv.Bg, surface = Rv.Card, onSurface = Rv.Text, onBackground = Rv.Text), content = content)
}

@Composable
fun AppRoot(askBluetooth: () -> Unit, askNotification: () -> Unit) {
    var tab by remember { mutableIntStateOf(0) }
    var showLog by remember { mutableStateOf(false) }
    Scaffold(
        containerColor = Rv.Bg,
        bottomBar = {
            NavigationBar(containerColor = Color(0xFF10131A)) {
                listOf("Home" to Icons.Filled.Home, "Trips" to Icons.AutoMirrored.Filled.List, "Settings" to Icons.Filled.Settings).forEachIndexed { i, (name, icon) ->
                    NavigationBarItem(
                        selected = tab == i, onClick = { tab = i }, icon = { Icon(icon, name) }, label = { Text(name, fontSize = 11.sp) },
                        colors = NavigationBarItemDefaults.colors(selectedIconColor = Rv.Acc, selectedTextColor = Rv.Acc, indicatorColor = Color(0xFF2A1A1A), unselectedIconColor = Rv.Dim, unselectedTextColor = Rv.Dim)
                    )
                }
            }
        }
    ) { pad ->
        Box(Modifier.padding(pad).fillMaxSize()) {
            when (tab) {
                0 -> HomeScreen(askBluetooth, askNotification) { showLog = true }
                1 -> TripsScreen()
                else -> Placeholder("Settings", "Units, warning levels, theme and gauge style of the board will be set here in a later update.")
            }
        }
    }
    if (showLog) AlertDialog(
        onDismissRequest = { showLog = false }, confirmButton = { TextButton(onClick = { showLog = false }) { Text("Close") } },
        title = { Text("Log") }, text = { Text(AppLog.text(), fontSize = 11.sp, modifier = Modifier.verticalScroll(rememberScrollState())) }
    )
}

@Composable
fun HomeScreen(askBluetooth: () -> Unit, askNotification: () -> Unit, openLog: () -> Unit) {
    var now by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(Unit) { while (true) { now = SystemClock.elapsedRealtime(); delay(500) } }       // so stale values disappear by themselves
    val live = AppState.live?.takeIf { now - AppState.liveAt < 4000 }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
        Spacer(Modifier.height(20.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text("Revsense", fontSize = 26.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f).pointerInput(Unit) { detectTapGestures(onLongPress = { openLog() }) })
            StatusPill(AppState.bleConnected)
        }
        Spacer(Modifier.height(14.dp))
        NavCard(live)
        Spacer(Modifier.height(18.dp))
        Text("LIVE", fontSize = 11.sp, color = Rv.Dim, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(8.dp))
        val kmh = live?.kmh ?: false; val tc = live?.tempC ?: false
        val g = listOf(
            Gauge("RPM", live?.rpm?.takeIf { it >= 0 }?.toString(), "", (live?.rpm ?: 0) / 8000f, Rv.Acc),
            Gauge("SPEED", live?.mph?.takeIf { it >= 0 }?.let { Fmt.speed(it, kmh).toString() }, if (kmh) "km/h" else "mph", Fmt.speed(live?.mph ?: 0, kmh) / (if (kmh) 260f else 160f), Rv.Acc),
            Gauge("COOLANT", live?.coolC?.takeIf { it >= 0 }?.let { Fmt.temp(it, tc).toString() }, if (tc) "°C" else "°F", ((live?.coolC ?: 40) - 40) / 90f, if ((live?.coolC ?: 0) >= 100) Rv.Warn else Rv.Acc),
            Gauge("BATTERY", live?.battDeciV?.takeIf { it >= 0 }?.let { "%.1f".format(it / 10.0) }, "V", ((live?.battDeciV ?: 105) / 10f - 10.5f) / 5f, if ((live?.battDeciV ?: 140) in 0..119) Rv.Warn else Rv.Acc),
            Gauge("FUEL", live?.fuel?.takeIf { it >= 0 }?.toString(), "%", (live?.fuel ?: 0) / 100f, Rv.Acc),
            Gauge("LOAD", live?.load?.takeIf { it >= 0 }?.toString(), "%", (live?.load ?: 0) / 100f, Rv.Acc),
            Gauge("INTAKE", live?.intakeC?.takeIf { it != NONE }?.let { Fmt.temp(it, tc).toString() }, if (tc) "°C" else "°F", ((live?.intakeC ?: -20) + 20) / 100f, Rv.Acc),
            torqueGauge(live, kmh)
        )
        for (r in 0 until 4) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GaugeCard(g[r * 2], Modifier.weight(1f)); GaugeCard(g[r * 2 + 1], Modifier.weight(1f))
            }
            Spacer(Modifier.height(8.dp))
        }
        Spacer(Modifier.height(8.dp))
        SetupCard(askBluetooth, askNotification)
        Spacer(Modifier.height(24.dp))
    }
}

/** Torque as the board shows it: real torque (lb-ft, or Nm with km/h units) when the car gives its reference torque, else percent. */
private fun torqueGauge(live: Live?, kmh: Boolean): Gauge {
    val nm = live?.torqueNm?.takeIf { it != NONE }; val pct = live?.torquePct?.takeIf { it != NONE }
    val frac = ((pct ?: -20) + 20) / 120f
    return when {
        nm != null -> Gauge("TORQUE", (if (kmh) nm else Math.round(nm * 0.737562).toInt()).toString(), if (kmh) "Nm" else "lb-ft", frac, Rv.Acc)
        pct != null -> Gauge("TORQUE", pct.toString(), "%", frac, Rv.Acc)
        else -> Gauge("TORQUE", null, "", 0f, Rv.Acc)
    }
}

@Composable
fun StatusPill(connected: Boolean) {
    val col = if (connected) Rv.Ok else Rv.Warn
    Row(Modifier.clip(RoundedCornerShape(50)).background(col.copy(alpha = 0.14f)).padding(horizontal = 12.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(8.dp).clip(CircleShape).background(col)); Spacer(Modifier.width(7.dp))
        Text(if (connected) "Connected" else "Searching…", fontSize = 12.sp, color = col, fontWeight = FontWeight.Bold)
    }
}

@Composable
fun Card(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier.clip(RoundedCornerShape(18.dp)).background(Rv.Card).border(1.dp, Rv.Edge, RoundedCornerShape(18.dp)).padding(16.dp), content = content)
}

@Composable
fun Bar(frac: Float, color: Color, modifier: Modifier = Modifier) {
    Box(modifier.fillMaxWidth().height(5.dp).clip(RoundedCornerShape(3.dp)).background(Rv.Track)) {
        Box(Modifier.fillMaxWidth(frac.coerceIn(0f, 1f)).fillMaxHeight().clip(RoundedCornerShape(3.dp)).background(color))
    }
}

data class Gauge(val name: String, val value: String?, val unit: String, val frac: Float, val color: Color)

@Composable
fun GaugeCard(g: Gauge, modifier: Modifier = Modifier) {
    Card(modifier) {
        Text(g.name, fontSize = 10.sp, color = Rv.Dim, fontWeight = FontWeight.Bold)
        Row(verticalAlignment = Alignment.Bottom) {
            Text(g.value ?: "--", fontSize = 30.sp, fontWeight = FontWeight.Bold, color = if (g.value == null) Rv.Dim else Rv.Text)
            if (g.unit.isNotEmpty()) { Spacer(Modifier.width(5.dp)); Text(g.unit, fontSize = 13.sp, color = Rv.Dim, modifier = Modifier.padding(bottom = 5.dp)) }
        }
        Spacer(Modifier.height(6.dp))
        Bar(if (g.value == null) 0f else g.frac, g.color)
    }
}

@Composable
fun NavCard(live: Live?) {
    var now by remember { mutableLongStateOf(SystemClock.elapsedRealtime()) }
    LaunchedEffect(Unit) { while (true) { now = SystemClock.elapsedRealtime(); delay(1000) } }
    val t = AppState.turn?.takeIf { now - AppState.turnAt < 20000 }
    Card(Modifier.fillMaxWidth()) {
        Text("NEXT TURN", fontSize = 10.sp, color = Rv.Dim, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(6.dp))
        if (t == null) {
            Text("No route", fontSize = 20.sp, fontWeight = FontWeight.Bold)
            Text("Start navigation in Google Maps and the turns appear here and on the board.", fontSize = 13.sp, color = Rv.Dim)
        } else {
            val (num, unit) = Fmt.dist(t.meters, live?.kmh ?: false)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Canvas(Modifier.size(86.dp)) { drawTurn(t.code, Rv.Acc) }
                Spacer(Modifier.width(14.dp))
                Column {
                    Row(verticalAlignment = Alignment.Bottom) {
                        Text(num, fontSize = 44.sp, fontWeight = FontWeight.Bold)
                        Spacer(Modifier.width(6.dp)); Text(unit, fontSize = 16.sp, color = Rv.Dim, modifier = Modifier.padding(bottom = 8.dp))
                    }
                    Text(t.street.ifEmpty { "—" }, fontSize = 16.sp, maxLines = 1)
                }
            }
            Spacer(Modifier.height(10.dp))
            Bar(Fmt.closeness(t.meters), Rv.Acc)
        }
    }
}

// The same arrows as the board's Navigation page (coordinates in a 150 x 150 box, y down).
private fun DrawScope.drawTurn(code: String, color: Color) {
    val s = size.width / 150f; val cx = size.width / 2f; val cy = size.height / 2f + 4 * s
    fun pt(x: Float, y: Float) = Offset(cx + x * s, cy + y * s)
    fun line(pts: List<Pair<Float, Float>>, dx: Float, mir: Float) {
        val path = Path()
        pts.forEachIndexed { i, (x, y) -> val o = pt(dx + mir * x, y - 8); if (i == 0) path.moveTo(o.x, o.y) else path.lineTo(o.x, o.y) }
        drawPath(path, color, style = Stroke(width = 13 * s, cap = StrokeCap.Butt, join = StrokeJoin.Round))
        val (ax, ay) = pts[pts.size - 2]; val (bx, by) = pts.last()
        var vx = mir * (bx - ax); var vy = by - ay; val l = Math.hypot(vx.toDouble(), vy.toDouble()).toFloat(); vx /= l; vy /= l
        val base = pt(dx + mir * bx, by - 8); val tip = Offset(base.x + vx * 30 * s, base.y + vy * 30 * s); val nx = -vy; val ny = vx
        val head = Path().apply { moveTo(base.x + nx * 17 * s, base.y + ny * 17 * s); lineTo(tip.x, tip.y); lineTo(base.x - nx * 17 * s, base.y - ny * 17 * s); close() }
        drawPath(head, color)
    }
    val m = if (code.endsWith("R")) -1f else 1f
    when (code) {
        "S" -> line(listOf(0f to 58f, 0f to -14f), 0f, 1f)
        "L", "R" -> line(listOf(0f to 58f, 0f to 8f, -40f to 8f), 30f * m, m)
        "SL", "SR" -> line(listOf(0f to 58f, 0f to 18f, -30f to -12f), 22f * m, m)
        "HL", "HR" -> line(listOf(0f to 58f, 0f to -8f, -34f to 26f), 24f * m, m)
        "UL", "UR" -> line(listOf(14f to 58f, 14f to -22f, -14f to -22f, -14f to 10f), 0f, m)
        "RB" -> {
            drawCircle(color, 24 * s, pt(0f, 22f - 8), style = Stroke(width = 8 * s))
            drawLine(color, pt(0f, 66f - 8), pt(0f, 50f - 8), strokeWidth = 13 * s)
            line(listOf(0f to -10f + 14f, 0f to -26f + 14f), 0f, 1f)
        }
        "A" -> { drawCircle(color, 35 * s, pt(0f, -8f), style = Stroke(width = 10 * s)); drawCircle(color, 15 * s, pt(0f, -8f)) }
    }
}

@Composable
fun SetupCard(askBluetooth: () -> Unit, askNotification: () -> Unit) {
    val ok = AppState.btPermission && AppState.listenerOn
    Text("SET UP", fontSize = 11.sp, color = Rv.Dim, fontWeight = FontWeight.Bold)
    Spacer(Modifier.height(8.dp))
    Card(Modifier.fillMaxWidth()) {
        if (ok) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(12.dp).clip(CircleShape).background(Rv.Ok)); Spacer(Modifier.width(12.dp))
                Text("Bluetooth and notification access are on", fontSize = 13.sp)
            }
        } else {
            if (!AppState.btPermission) SetupButton("Allow Bluetooth", askBluetooth)
            if (!AppState.listenerOn) { if (!AppState.btPermission) Spacer(Modifier.height(8.dp)); SetupButton("Allow notification access", askNotification) }
            Spacer(Modifier.height(6.dp))
            Text("Notification access lets the app read the Google Maps turn instructions. Nothing leaves your phone except to the board.", fontSize = 11.sp, color = Rv.Dim)
        }
    }
}

@Composable
fun SetupButton(text: String, onClick: () -> Unit) {
    Button(onClick, Modifier.fillMaxWidth(), colors = ButtonDefaults.buttonColors(containerColor = Rv.Acc, contentColor = Color.White), shape = RoundedCornerShape(12.dp)) { Text(text) }
}

@Composable
fun Placeholder(title: String, text: String) {
    Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(title, fontSize = 26.sp, fontWeight = FontWeight.Bold)
        Spacer(Modifier.height(10.dp))
        Text(text, color = Rv.Dim, fontSize = 14.sp, textAlign = TextAlign.Center)
    }
}

@Composable
fun TripsScreen() {
    val connected = AppState.bleConnected
    LaunchedEffect(connected) { while (connected) { BleLink.requestTrips(); delay(10_000) } }        // load now, then refresh the running trip every 10 s
    val trips = AppState.trips; val live = AppState.live
    val kmh = live?.kmh ?: false; val tc = live?.tempC ?: false; val bar = ((live?.units ?: 0) and 4) != 0
    var sel by remember { mutableIntStateOf(0) }
    if (sel >= trips.size) sel = 0
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
        Spacer(Modifier.height(20.dp))
        Text("Trips", fontSize = 26.sp, fontWeight = FontWeight.Bold)
        Text(if (connected) "Last 5 drives kept on the board" else "Connect to the board to load your trips", fontSize = 13.sp, color = Rv.Dim)
        Spacer(Modifier.height(14.dp))
        if (trips.isEmpty()) {
            Card(Modifier.fillMaxWidth()) {
                Text("No trips yet", fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(4.dp))
                Text("A trip is saved after you drive and the engine stops (at least 90 seconds and 0.2 mi). It then shows up here.", fontSize = 13.sp, color = Rv.Dim)
            }
        } else {
            val t = trips[sel]
            Card(Modifier.fillMaxWidth()) {
                Text(tripTitle(t).uppercase(), fontSize = 10.sp, color = Rv.Dim, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(4.dp))
                Text(TripFmt.distance(t.distCentiMi, kmh) + " in " + TripFmt.duration(t.durationS), fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(8.dp))
                ReportRow("Top speed", TripFmt.speed(t.topMph, kmh))
                ReportRow("Max RPM", t.maxRpm.toString())
                ReportRow("Max coolant", TripFmt.coolant(t.maxCoolC, tc))
                ReportRow("Peak boost", TripFmt.boost(t.peakBoostDeciPsi, bar))
                ReportRow("Battery", TripFmt.battery(t.minBattDeciV, t.maxBattDeciV))
                ReportRow("New codes", TripFmt.codes(t.codes), if (t.codes.isEmpty()) Rv.Ok else Rv.Warn)
            }
            if (trips.size > 1) {
                Spacer(Modifier.height(18.dp))
                Text("ALL TRIPS", fontSize = 11.sp, color = Rv.Dim, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(8.dp))
                trips.forEachIndexed { i, r ->
                    Row(
                        Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(if (i == sel) Color(0xFF241A1C) else Rv.Card)
                            .border(1.dp, if (i == sel) Rv.Acc else Rv.Edge, RoundedCornerShape(16.dp)).clickable { sel = i }.padding(14.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(tripTitle(r), fontSize = 14.sp, fontWeight = FontWeight.Bold)
                            Text(TripFmt.distance(r.distCentiMi, kmh) + " · " + TripFmt.duration(r.durationS), fontSize = 12.sp, color = Rv.Dim)
                        }
                        Text(TripFmt.speed(r.topMph, kmh), fontSize = 13.sp, color = Rv.Acc, fontWeight = FontWeight.Bold)
                    }
                    Spacer(Modifier.height(8.dp))
                }
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}

private fun tripTitle(t: TripRec): String {
    val seen = TripStore.whenSeen(t.seq)
    return if (t.live) "Current trip · live" else "Trip #${t.seq}" + (seen?.let { " · $it" } ?: "")
}

@Composable
fun ReportRow(label: String, value: String, valueColor: Color = Rv.Text) {
    Column {
        Box(Modifier.fillMaxWidth().height(1.dp).background(Rv.Edge))
        Row(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
            Text(label, fontSize = 13.sp, color = Rv.Dim, modifier = Modifier.weight(1f))
            Text(value, fontSize = 14.sp, fontWeight = FontWeight.Bold, color = valueColor)
        }
    }
}
