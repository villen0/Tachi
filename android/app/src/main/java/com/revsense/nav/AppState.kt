package com.revsense.nav

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/** What the screens show. Written from the Bluetooth / notification callbacks, read by Compose. */
object AppState {
    var live by mutableStateOf<Live?>(null)
    var liveAt by mutableLongStateOf(0L)          // SystemClock.elapsedRealtime() of the last live line
    var trips by mutableStateOf<List<TripRec>>(emptyList())     // the last list the board sent, newest first (the running trip leads)
    var tripsAt by mutableLongStateOf(0L)
    var config by mutableStateOf<BoardConfig?>(null)             // the board's settings as it last reported them
    var turn by mutableStateOf<Turn?>(null)
    var turnAt by mutableLongStateOf(0L)
    var bleConnected by mutableStateOf(false)
    var bleText by mutableStateOf("not started")
    var listenerOn by mutableStateOf(false)
    var btPermission by mutableStateOf(false)
}
