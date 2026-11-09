package com.revsense.nav

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import java.util.UUID

/**
 * The Bluetooth link to the board: finds the "Revsense" service, connects, and writes turn messages to its NAV characteristic.
 * It reconnects by itself when the board goes away (engine off, out of range) and comes back.
 */
@SuppressLint("MissingPermission")
object BleLink {
    val SERVICE: UUID = UUID.fromString("52657673-656e-7365-0000-4e4156000001")
    val CHAR: UUID = UUID.fromString("52657673-656e-7365-0000-4e4156000002")
    val LIVE: UUID = UUID.fromString("52657673-656e-7365-0000-4e4156000003")
    val TRIPS: UUID = UUID.fromString("52657673-656e-7365-0000-4e4156000004")
    val CONFIG: UUID = UUID.fromString("52657673-656e-7365-0000-4e4156000005")
    private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private lateinit var ctx: Context
    private val main = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var nav: BluetoothGattCharacteristic? = null
    private val pendingSubs = ArrayDeque<BluetoothGattCharacteristic>()
    private val tripBuf = ArrayList<TripRec>()
    private var scanning = false
    private var started = false

    val connected: Boolean get() = nav != null

    fun hasPermission(c: Context): Boolean =
        if (Build.VERSION.SDK_INT >= 31) c.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
            c.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        else c.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    fun start(c: Context) {
        ctx = c.applicationContext; TripStore.init(ctx)
        if (!hasPermission(ctx)) { AppLog.setBle("no Bluetooth permission yet"); return }
        if (started) return
        started = true; scan()
    }

    private fun adapter(): BluetoothAdapter? = (ctx.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    private fun scan() {
        val a = adapter()
        if (a == null || !a.isEnabled) { AppLog.setBle("Bluetooth is off"); main.postDelayed({ scan() }, 5000); return }
        val sc = a.bluetoothLeScanner ?: run { main.postDelayed({ scan() }, 5000); return }
        if (scanning) return
        scanning = true; AppLog.setBle("looking for the board...")
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()
        sc.startScan(listOf(filter), ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), scanCb)
    }

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(type: Int, r: ScanResult) {
            adapter()?.bluetoothLeScanner?.stopScan(this); scanning = false
            AppLog.setBle("found the board, connecting...")
            gatt = r.device.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE)
        }
        override fun onScanFailed(code: Int) { scanning = false; AppLog.setBle("scan failed ($code)"); main.postDelayed({ scan() }, 5000) }
    }

    private val gattCb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) g.requestMtu(185)          // room for a long street name
            else { nav = null; AppState.live = null; g.close(); gatt = null; AppLog.setBle("board disconnected"); main.postDelayed({ scan() }, 2000) }
        }
        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) { g.discoverServices() }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val ch = g.getService(SERVICE)?.getCharacteristic(CHAR)
            if (ch == null) { AppLog.setBle("the board has no NAV service (old firmware?)"); g.disconnect(); return }
            ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            nav = ch; AppLog.setBle("connected to the board")
            val svc = g.getService(SERVICE)                                                    // subscribe to the live values and the trips (older firmware has none)
            pendingSubs.clear(); listOfNotNull(svc?.getCharacteristic(LIVE), svc?.getCharacteristic(TRIPS), svc?.getCharacteristic(CONFIG)).forEach { pendingSubs.add(it) }
            subscribeNext(g)
        }
        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) { subscribeNext(g) }
        private fun subscribeNext(g: BluetoothGatt) {
            val c = pendingSubs.removeFirstOrNull() ?: run { requestTrips(); requestConfig(); return }
            val cccd = c.getDescriptor(CCCD) ?: run { subscribeNext(g); return }
            g.setCharacteristicNotification(c, true)
            if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            else { @Suppress("DEPRECATION") run { cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; g.writeDescriptor(cccd) } }
        }
        private fun incoming(c: BluetoothGattCharacteristic, b: ByteArray?) {
            if (b == null) return
            when (c.uuid) { TRIPS -> trips(String(b)); CONFIG -> ConfigParser.parse(String(b))?.let { AppState.config = it }; else -> live(b) }
        }
        private fun trips(line: String) {
            if (line.startsWith("E|")) { AppState.trips = tripBuf.toList(); AppState.tripsAt = SystemClock.elapsedRealtime(); tripBuf.clear(); return }
            val t = TripParser.parse(line) ?: return
            TripStore.note(t.seq, t.live); tripBuf.add(t)
        }
        private fun live(b: ByteArray?) { val l = b?.let { LiveParser.parse(String(it)) } ?: return; AppState.live = l; AppState.liveAt = SystemClock.elapsedRealtime() }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) = incoming(c, value)
        @Deprecated("old callback") override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) { @Suppress("DEPRECATION") incoming(c, c.value) }
    }

    /** Asks the board for its trips; the answer arrives as notifications (see Trips.kt). */
    fun requestTrips() { tripBuf.clear(); send("Q|T") }

    /** Asks the board for its settings; the answer arrives on the CONFIG characteristic. */
    fun requestConfig() { send("S?") }

    /** Changes one setting on the board, e.g. change("sp", 1); the board answers with its new settings. Keys: see src/nav.h in the firmware. */
    fun change(key: String, value: Int) { send("S|$key|$value") }

    // Messages go out one at a time with a short gap (Bluetooth drops back-to-back writes).
    private val outQ = ArrayDeque<String>()
    private var draining = false

    /** Queues one message line for the board. Returns false if there is no link right now. */
    fun send(text: String): Boolean {
        if (gatt == null || nav == null) return false
        synchronized(outQ) { outQ.add(text); if (draining) return true; draining = true }
        main.post { drain() }
        return true
    }

    private fun drain() {
        val t = synchronized(outQ) { outQ.removeFirstOrNull().also { if (it == null) draining = false } } ?: return
        rawSend(t)
        main.postDelayed({ drain() }, 60)
    }

    private fun rawSend(text: String): Boolean {
        val g = gatt ?: return false; val ch = nav ?: return false
        val bytes = text.toByteArray(Charsets.UTF_8)
        return if (Build.VERSION.SDK_INT >= 33) g.writeCharacteristic(ch, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) == BluetoothStatusCodes.SUCCESS
        else { @Suppress("DEPRECATION") run { ch.value = bytes; g.writeCharacteristic(ch) } }
    }
}
