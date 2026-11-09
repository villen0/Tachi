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
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.drawscope.Stroke as DrawStroke
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
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
                listOf("Home" to Icons.Filled.Home, "Trips" to Icons.AutoMirrored.Filled.List, "Codes" to Icons.Filled.Warning, "Settings" to Icons.Filled.Settings).forEachIndexed { i, (name, icon) ->
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
                2 -> CodesScreen()
                else -> SettingsScreen()
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
            Row(Modifier.weight(1f).pointerInput(Unit) { detectTapGestures(onLongPress = { openLog() }) }, verticalAlignment = Alignment.CenterVertically) {   // long-press: the log
                RevsenseMark(38.dp); Spacer(Modifier.width(10.dp)); Wordmark(26.sp)
            }
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

@Composable
fun SettingsScreen() {
    val c = AppState.config
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
        Spacer(Modifier.height(20.dp))
        Text("Settings", fontSize = 26.sp, fontWeight = FontWeight.Bold)
        Text(if (AppState.bleConnected) "Saved on the board" else "Connect to the board to change its settings", fontSize = 13.sp, color = Rv.Dim)
        Spacer(Modifier.height(14.dp))
        if (c == null) {
            Card(Modifier.fillMaxWidth()) {
                Text("Not connected", fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(4.dp))
                Text("Once the board is connected, its units, warning levels, look and brightness show up here.", fontSize = 13.sp, color = Rv.Dim)
            }
        } else {
            SectionLabel("UNITS")
            SegRow("Speed", listOf("mph", "km/h"), if (c.speedKmh) 1 else 0) { BleLink.change("sp", it) }
            SegRow("Temperature", listOf("°F", "°C"), if (c.tempC) 1 else 0) { BleLink.change("tp", it) }
            SegRow("Pressure", listOf("psi", "bar"), if (c.pressBar) 1 else 0) { BleLink.change("pr", it) }
            Spacer(Modifier.height(14.dp))
            SectionLabel("WARNINGS")
            StepRow("Coolant warm", "${Fmt.temp(c.coolWarnC, c.tempC)} ${if (c.tempC) "°C" else "°F"}") { BleLink.change("wc", it) }
            StepRow("Coolant hot", "${Fmt.temp(c.coolHotC, c.tempC)} ${if (c.tempC) "°C" else "°F"}") { BleLink.change("wh", it) }
            StepRow("Battery low", "%.1f V".format(c.battLowDeciV / 10.0)) { BleLink.change("wb", it) }
            StepRow("Fuel low", "${c.fuelLowPct} %") { BleLink.change("wf", it) }
            StepRow("Redline", c.redlineRpm.toString()) { BleLink.change("wr", it) }
            Spacer(Modifier.height(14.dp))
            SectionLabel("LOOK")
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Names.looks.forEachIndexed { i, n ->
                    Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                        Box(
                            Modifier.fillMaxWidth().height(46.dp).clip(RoundedCornerShape(14.dp)).background(Color(Names.looksRgb[i]))
                                .border(if (c.theme == i) 2.dp else 0.dp, Rv.Text, RoundedCornerShape(14.dp)).clickable { BleLink.change("th", i) }
                        )
                        Spacer(Modifier.height(4.dp))
                        Text(n, fontSize = 9.sp, color = if (c.theme == i) Rv.Text else Rv.Dim, textAlign = TextAlign.Center, maxLines = 2)
                    }
                }
            }
            Spacer(Modifier.height(14.dp))
            SectionLabel("COLOUR")
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Names.modes.forEachIndexed { i, n ->
                    Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                        Box(
                            Modifier.size(34.dp).clip(CircleShape).background(if (i == 7) Color(0xFF000000.toInt() or c.customRgb) else Color(Names.modesRgb[i]))
                                .border(if (c.mode == i) 2.dp else 0.dp, Rv.Text, CircleShape).clickable { BleLink.change("md", i) }
                        )
                        Spacer(Modifier.height(3.dp))
                        Text(n, fontSize = 9.sp, color = if (c.mode == i) Rv.Text else Rv.Dim, maxLines = 1)
                    }
                }
            }
            Spacer(Modifier.height(14.dp))
            SectionLabel("CUSTOM COLOUR")
            var shown by remember(c.customRgb) { mutableIntStateOf(c.customRgb) }
            var lastSend by remember { mutableLongStateOf(0L) }
            val pick = { rgb: Int, fin: Boolean ->
                shown = rgb; val t = SystemClock.elapsedRealtime()
                if (fin || t - lastSend >= 250) { lastSend = t; BleLink.change("cc", rgb) }                  // live while dragging (about 4 times a second), and once more on release
            }
            Card(Modifier.fillMaxWidth()) { Text("Standard colorwheel", fontSize = 14.sp, color = Rv.Dim); Spacer(Modifier.height(10.dp)); StandardWheel(shown, pick) }
            Spacer(Modifier.height(10.dp))
            Card(Modifier.fillMaxWidth()) { Text("Gradient colorwheel", fontSize = 14.sp, color = Rv.Dim); Spacer(Modifier.height(10.dp)); GradientWheel(shown, pick) }
            Spacer(Modifier.height(14.dp))
            SectionLabel("GAUGE STYLE")
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Names.gauges.forEachIndexed { i, n ->
                    Box(
                        Modifier.weight(1f).height(40.dp).clip(RoundedCornerShape(12.dp)).background(if (c.gauge == i) Rv.Acc else Rv.Card)
                            .border(1.dp, Rv.Edge, RoundedCornerShape(12.dp)).clickable { BleLink.change("ga", i) },
                        contentAlignment = Alignment.Center
                    ) { Text(n, fontSize = 13.sp, fontWeight = FontWeight.Bold) }
                }
            }
            Spacer(Modifier.height(14.dp))
            SectionLabel("BRIGHTNESS")
            var br by remember(c.brightness) { mutableFloatStateOf(c.brightness.toFloat()) }
            Card(Modifier.fillMaxWidth()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Slider(
                        value = br, onValueChange = { br = it }, valueRange = 5f..100f, onValueChangeFinished = { BleLink.change("br", br.toInt()) },
                        modifier = Modifier.weight(1f), colors = SliderDefaults.colors(thumbColor = Rv.Acc, activeTrackColor = Rv.Acc, inactiveTrackColor = Rv.Track)
                    )
                    Spacer(Modifier.width(10.dp)); Text("${br.toInt()} %", fontSize = 13.sp, fontWeight = FontWeight.Bold)
                }
            }
        }
        Spacer(Modifier.height(24.dp))
    }
}

@Composable
fun SectionLabel(t: String) { Text(t, fontSize = 11.sp, color = Rv.Dim, fontWeight = FontWeight.Bold); Spacer(Modifier.height(8.dp)) }

@Composable
fun SegRow(label: String, options: List<String>, selected: Int, onPick: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(bottom = 8.dp).clip(RoundedCornerShape(14.dp)).background(Rv.Card).border(1.dp, Rv.Edge, RoundedCornerShape(14.dp)).padding(horizontal = 14.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, fontSize = 14.sp, modifier = Modifier.weight(1f))
        Row(Modifier.clip(RoundedCornerShape(10.dp)).background(Rv.Track)) {
            options.forEachIndexed { i, o ->
                Box(
                    Modifier.width(62.dp).height(30.dp).padding(2.dp).clip(RoundedCornerShape(9.dp)).background(if (i == selected) Rv.Acc else Color.Transparent).clickable { onPick(i) },
                    contentAlignment = Alignment.Center
                ) { Text(o, fontSize = 12.sp, fontWeight = FontWeight.Bold) }
            }
        }
    }
}

@Composable
fun StepRow(label: String, value: String, onStep: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(bottom = 8.dp).clip(RoundedCornerShape(14.dp)).background(Rv.Card).border(1.dp, Rv.Edge, RoundedCornerShape(14.dp)).padding(horizontal = 14.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, fontSize = 14.sp, modifier = Modifier.weight(1f))
        StepButton("–") { onStep(-1) }
        Text(value, fontSize = 13.sp, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center, modifier = Modifier.width(78.dp))
        StepButton("+") { onStep(1) }
    }
}

@Composable
fun StepButton(t: String, onClick: () -> Unit) {
    Box(Modifier.size(34.dp).clip(RoundedCornerShape(10.dp)).background(Rv.Track).clickable { onClick() }, contentAlignment = Alignment.Center) { Text(t, fontSize = 18.sp, fontWeight = FontWeight.Bold) }
}

/** The logo's gauge (the one the board draws on its welcome screen): white ring, red arc with a grey rest of the track, needle and hub. */
@Composable
fun RevsenseMark(size: Dp, modifier: Modifier = Modifier) {
    Canvas(modifier.size(size)) {
        val s = this.size.minDimension / 2f / 322f; val c = center                       // the logo's units: ring outer radius 312, stroke 25
        fun arc(r: Float, w: Float, a0: Float, sweep: Float, col: Color) =
            drawArc(col, a0 - 90f, sweep, false, Offset(c.x - r * s, c.y - r * s), Size(2 * r * s, 2 * r * s), style = DrawStroke(w * s, cap = StrokeCap.Round))
        drawCircle(Color.White, 299.5f * s, c, style = DrawStroke(25f * s))
        arc(218f, 37f, 29.7f, 105.3f, Color(0xFF4A4A4E))                                 // the rest of the track, clockwise from 12 o'clock
        arc(218f, 37f, -135f, 164.7f, Rv.Acc)                                            // the red fill
        drawLine(Color.White, c, Offset(c.x + 85f * s, c.y - 147f * s), strokeWidth = 18f * s, cap = StrokeCap.Round)
        drawCircle(Rv.Acc, 30f * s, c)
    }
}

/** "Rev" in white, "sense" in the logo red, like the welcome screen. */
@Composable
fun Wordmark(fontSize: androidx.compose.ui.unit.TextUnit) {
    Text(buildAnnotatedString { withStyle(SpanStyle(color = Rv.Text)) { append("Rev") }; withStyle(SpanStyle(color = Rv.Acc)) { append("sense") } }, fontSize = fontSize, fontWeight = FontWeight.Medium)
}

@Composable
fun CodesScreen() {
    val connected = AppState.bleConnected
    LaunchedEffect(connected) { if (connected) BleLink.requestCodes() }
    val st = AppState.codes
    var confirm by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp)) {
        Spacer(Modifier.height(20.dp))
        Text("Fault codes", fontSize = 26.sp, fontWeight = FontWeight.Bold)
        Text(if (connected) "Read from the car by the board" else "Connect to the board to read the codes", fontSize = 13.sp, color = Rv.Dim)
        Spacer(Modifier.height(14.dp))
        if (st == null || !st.answered) {
            Card(Modifier.fillMaxWidth()) {
                Text(if (st == null) "Not read yet" else "The car did not answer", fontSize = 20.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(4.dp))
                Text(st?.status?.ifEmpty { null } ?: "Connect the board to the car, then read the codes.", fontSize = 13.sp, color = Rv.Dim)
            }
        } else {
            Card(Modifier.fillMaxWidth()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(14.dp).clip(CircleShape).background(if (st.mil) Rv.Warn else Rv.Ok)); Spacer(Modifier.width(12.dp))
                    Column {
                        Text(if (st.mil) "Check engine light is ON" else "Check engine light is off", fontSize = 16.sp, fontWeight = FontWeight.Bold)
                        Text(if (st.total == 0) "No fault codes stored" else "${st.total} code${if (st.total == 1) "" else "s"}", fontSize = 13.sp, color = Rv.Dim)
                    }
                }
            }
            Spacer(Modifier.height(10.dp))
            st.rows.forEach { r ->
                Row(Modifier.fillMaxWidth().padding(bottom = 8.dp).clip(RoundedCornerShape(16.dp)).background(Rv.Card).border(1.dp, Rv.Edge, RoundedCornerShape(16.dp)).padding(14.dp), verticalAlignment = Alignment.Top) {
                    Column(Modifier.weight(1f)) {
                        Text(r.code, fontSize = 20.sp, fontWeight = FontWeight.Bold)
                        Text(r.desc.ifEmpty { "No description in the board's list" }, fontSize = 13.sp, color = Rv.Dim)
                    }
                    val col = if (r.isNew) Rv.Acc else if (r.kind == 1) Rv.Warn else Rv.Dim
                    Text(r.tag, fontSize = 10.sp, fontWeight = FontWeight.Bold, color = col, modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(col.copy(alpha = 0.15f)).padding(horizontal = 8.dp, vertical = 4.dp))
                }
            }
        }
        if (st != null && st.status.isNotEmpty()) { Spacer(Modifier.height(4.dp)); Text(st.status, fontSize = 12.sp, color = Rv.Dim) }
        Spacer(Modifier.height(14.dp))
        SetupButton("Read the codes again") { BleLink.requestCodes() }
        Spacer(Modifier.height(8.dp))
        Box(
            Modifier.fillMaxWidth().height(44.dp).clip(RoundedCornerShape(12.dp)).border(1.dp, Rv.Acc, RoundedCornerShape(12.dp)).clickable(enabled = connected) { confirm = true },
            contentAlignment = Alignment.Center
        ) { Text("Clear codes…", color = Rv.Acc, fontWeight = FontWeight.Bold) }
        Spacer(Modifier.height(24.dp))
    }
    if (confirm) AlertDialog(
        onDismissRequest = { confirm = false },
        title = { Text("Clear the fault codes?") },
        text = { Text("The engine must be off (the board refuses otherwise). Clearing also resets the car's readiness monitors and freeze-frame data, so only do it once the problem is fixed.") },
        confirmButton = { TextButton(onClick = { confirm = false; BleLink.clearCodes() }) { Text("Clear", color = Rv.Acc) } },
        dismissButton = { TextButton(onClick = { confirm = false }) { Text("Cancel") } }
    )
}
