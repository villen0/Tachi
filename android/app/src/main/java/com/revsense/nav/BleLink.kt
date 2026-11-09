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
    private val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private lateinit var ctx: Context
    private val main = Handler(Looper.getMainLooper())
    private var gatt: BluetoothGatt? = null
    private var nav: BluetoothGattCharacteristic? = null
    private var scanning = false
    private var started = false

    val connected: Boolean get() = nav != null

    fun hasPermission(c: Context): Boolean =
        if (Build.VERSION.SDK_INT >= 31) c.checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED &&
            c.checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        else c.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED

    fun start(c: Context) {
        ctx = c.applicationContext
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
            val live = g.getService(SERVICE)?.getCharacteristic(LIVE)                       // subscribe to the live values (older firmware has none)
            val cccd = live?.getDescriptor(CCCD)
            if (live != null && cccd != null) {
                g.setCharacteristicNotification(live, true)
                if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                else { @Suppress("DEPRECATION") run { cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; g.writeDescriptor(cccd) } }
            }
        }
        private fun live(b: ByteArray?) { val l = b?.let { LiveParser.parse(String(it)) } ?: return; AppState.live = l; AppState.liveAt = SystemClock.elapsedRealtime() }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) = live(value)
        @Deprecated("old callback") override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) { @Suppress("DEPRECATION") live(c.value) }
    }

    /** Sends one message line to the board. Returns false if there is no link right now. */
    fun send(text: String): Boolean {
        val g = gatt ?: return false; val ch = nav ?: return false
        val bytes = text.toByteArray(Charsets.UTF_8)
        return if (Build.VERSION.SDK_INT >= 33) g.writeCharacteristic(ch, bytes, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE) == BluetoothStatusCodes.SUCCESS
        else { @Suppress("DEPRECATION") run { ch.value = bytes; g.writeCharacteristic(ch) } }
    }
}
