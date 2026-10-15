package com.bhubit.g7bridge.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import java.io.IOException
import java.util.UUID

/**
 * Talks to the bridge's setup service (firmware/src/g7_ui.c): reads the status JSON,
 * writes commands and uploads the app certificates. One GATT operation at a time.
 *
 * The phone usually already shares a bond with the bridge through xDrip+; Android lets
 * both apps use the same link. A new bond is only accepted by the bridge within
 * 10 minutes of powering it up.
 */
@SuppressLint("MissingPermission") // the UI asks for the Bluetooth permissions first
class BridgeClient(context: Context, private val scope: CoroutineScope) {

    enum class Conn { DISCONNECTED, BONDING, CONNECTING, CONNECTED }

    sealed interface Problem {
        /** The bridge refused the pairing: its 10-minute window has passed. */
        data object Denied : Problem
        data object NotABridge : Problem
        data class Lost(val retrying: Boolean) : Problem
        data class ConnectFailed(val reason: String) : Problem
        data class ReadFailed(val reason: String) : Problem
        data class SendFailed(val reason: String) : Problem
    }

    private val ctx = context.applicationContext
    private val adapter = ctx.getSystemService(BluetoothManager::class.java)?.adapter

    private val _conn = MutableStateFlow(Conn.DISCONNECTED)
    val conn: StateFlow<Conn> = _conn
    private val _status = MutableStateFlow<BridgeStatus?>(null)
    val status: StateFlow<BridgeStatus?> = _status
    /** From Device Information (older firmware has no "fw" in the status). */
    private val _fwRevision = MutableStateFlow<String?>(null)
    val fwRevision: StateFlow<String?> = _fwRevision
    private val _canLoadCerts = MutableStateFlow(false)
    val canLoadCerts: StateFlow<Boolean> = _canLoadCerts
    private val _problems = MutableSharedFlow<Problem>(extraBufferCapacity = 8)
    val problems: SharedFlow<Problem> = _problems

    /** Time between status reads while the app is on screen. */
    private val pollIntervalMs = 5_000L
    /** Status reads pause while the app is not on screen. */
    @Volatile var polling = true
        set(value) {
            field = value
            if (value) wake.trySend(Unit)
        }

    val bluetoothOn get() = adapter?.isEnabled == true

    private var loop: Job? = null
    @Volatile private var wanted: String? = null
    @Volatile private var gatt: BluetoothGatt? = null
    private var statusChr: BluetoothGattCharacteristic? = null
    private var cmdChr: BluetoothGattCharacteristic? = null
    private var certsChr: BluetoothGattCharacteristic? = null

    private val opLock = Mutex()
    @Volatile private var pendingOp: CompletableDeferred<Result>? = null
    @Volatile private var linkUp: CompletableDeferred<Unit>? = null
    @Volatile private var linkDown = CompletableDeferred<Unit>()
    private val wake = Channel<Unit>(Channel.CONFLATED)

    private class Result(val status: Int, val value: ByteArray?)

    // ---- choosing a bridge --------------------------------------------------------

    /** Bridges already bonded with this phone (e.g. through xDrip+). */
    fun bondedBridges(): List<BluetoothDevice> =
        adapter?.bondedDevices.orEmpty().filter { it.name == NAME }

    private var scanCb: ScanCallback? = null

    fun startScan(onFound: (BluetoothDevice) -> Unit) {
        val scanner = adapter?.bluetoothLeScanner ?: return
        stopScan()
        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) = onFound(result.device)
        }
        scanCb = cb
        scanner.startScan(
            listOf(ScanFilter.Builder().setDeviceName(NAME).build()),
            ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(),
            cb,
        )
    }

    fun stopScan() {
        scanCb?.let { adapter?.bluetoothLeScanner?.stopScan(it) }
        scanCb = null
    }

    // ---- connection ---------------------------------------------------------------

    /** Connect and stay connected (reconnecting by itself) until [disconnect]. */
    fun connect(address: String) {
        if (wanted == address && loop?.isActive == true) return
        wanted = address
        loop?.cancel()
        loop = scope.launch {
            while (wanted == address) {
                try {
                    runSession(address)
                } catch (e: CancellationException) {
                    closeGatt()
                    throw e
                } catch (e: DeniedException) {
                    closeGatt()
                    _problems.tryEmit(Problem.Denied)
                    wanted = null
                    break
                } catch (e: NotABridgeException) {
                    closeGatt()
                    _problems.tryEmit(Problem.NotABridge)
                    wanted = null
                    break
                } catch (e: Exception) {
                    val wasConnected = _conn.value == Conn.CONNECTED
                    closeGatt()
                    _problems.tryEmit(
                        if (wasConnected) Problem.Lost(retrying = wanted != null)
                        else Problem.ConnectFailed(e.message ?: e.javaClass.simpleName),
                    )
                }
                if (wanted != address) break
                delay(4_000)
            }
        }
    }

    fun disconnect() {
        wanted = null
        loop?.cancel()
        loop = null
        closeGatt()
    }

    private suspend fun runSession(address: String) {
        val device = adapter?.getRemoteDevice(address) ?: throw IOException("Bluetooth off")
        if (device.bondState != BluetoothDevice.BOND_BONDED) {
            _conn.value = Conn.BONDING
            bond(device)
        }
        _conn.value = Conn.CONNECTING
        open(device)
        _conn.value = Conn.CONNECTED

        while (!linkDown.isCompleted) {
            if (polling) refreshStatus()
            withTimeoutOrNull(pollIntervalMs) { wake.receive() }
        }
        throw IOException("disconnected")
    }

    /**
     * One status read for the background check: only an already paired bridge (never asks
     * to pair in the background), and the link is closed again right after.
     */
    suspend fun readStatusOnce(address: String): BridgeStatus {
        val device = adapter?.takeIf { it.isEnabled }?.getRemoteDevice(address)
            ?: throw IOException("Bluetooth off")
        if (device.bondState != BluetoothDevice.BOND_BONDED) throw IOException("not paired")
        try {
            open(device)
            val r = op { readChr(it, statusChr ?: throw IOException("disconnected")) }
            if (r.status != BluetoothGatt.GATT_SUCCESS) throw IOException("GATT status ${r.status}")
            return BridgeStatus.parse(r.value?.decodeToString() ?: "")
        } finally {
            closeGatt()
        }
    }

    /** Connects and finds the setup service (and the firmware version). */
    private suspend fun open(device: BluetoothDevice) {
        val up = CompletableDeferred<Unit>()
        linkUp = up
        linkDown = CompletableDeferred()
        gatt = device.connectGatt(ctx, false, callback, BluetoothDevice.TRANSPORT_LE)
            ?: throw IOException("connectGatt failed")
        withTimeout(20_000) { up.await() }

        op { it.requestMtu(247) } // larger writes and fewer long-read rounds; failure is fine
        val discovered = op { it.discoverServices() }
        if (discovered.status != BluetoothGatt.GATT_SUCCESS) throw IOException("service discovery failed")
        val g = gatt ?: throw IOException("disconnected")
        val svc = g.getService(SVC) ?: throw NotABridgeException()
        statusChr = svc.getCharacteristic(STATUS) ?: throw NotABridgeException()
        cmdChr = svc.getCharacteristic(CMD) ?: throw NotABridgeException()
        certsChr = svc.getCharacteristic(CERTS) // older firmware: none
        _canLoadCerts.value = certsChr != null
        g.getService(DIS)?.getCharacteristic(FW_REV)?.let { chr ->
            val r = op { readChr(it, chr) }
            if (r.status == BluetoothGatt.GATT_SUCCESS) {
                _fwRevision.value = r.value?.decodeToString()?.trim()
            }
        }
    }

    /** Pair with the bridge; it only accepts within 10 minutes of power-up. */
    private suspend fun bond(device: BluetoothDevice) {
        val done = CompletableDeferred<Int>()
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                @Suppress("DEPRECATION")
                val d: BluetoothDevice? = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                if (d?.address != device.address) return
                when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1)) {
                    BluetoothDevice.BOND_BONDED -> done.complete(BluetoothDevice.BOND_BONDED)
                    BluetoothDevice.BOND_NONE -> done.complete(BluetoothDevice.BOND_NONE)
                }
            }
        }
        ContextCompat.registerReceiver(
            ctx, receiver, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED),
            ContextCompat.RECEIVER_EXPORTED,
        )
        try {
            if (!device.createBond() && device.bondState != BluetoothDevice.BOND_BONDING) {
                throw IOException("could not start pairing")
            }
            val result = withTimeout(90_000) { done.await() }
            if (result != BluetoothDevice.BOND_BONDED) throw DeniedException()
        } finally {
            ctx.unregisterReceiver(receiver)
        }
    }

    private fun closeGatt() {
        val g = gatt
        gatt = null
        statusChr = null
        cmdChr = null
        certsChr = null
        _canLoadCerts.value = false
        pendingOp?.completeExceptionally(IOException("disconnected"))
        linkDown.complete(Unit)
        g?.disconnect()
        g?.close()
        _conn.value = Conn.DISCONNECTED
    }

    // ---- reads and writes -----------------------------------------------------------

    suspend fun refreshStatus() {
        val chr = statusChr ?: return
        try {
            val r = op { readChr(it, chr) }
            if (r.status != BluetoothGatt.GATT_SUCCESS) throw IOException("GATT status ${r.status}")
            _status.value = BridgeStatus.parse(r.value?.decodeToString() ?: "")
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            // A JSON error message carries the whole text: keep the line short
            val reason = if (e is org.json.JSONException) "incomplete status" else e.message ?: "?"
            if (!linkDown.isCompleted) _problems.tryEmit(Problem.ReadFailed(reason))
        }
    }

    /** A setup command (see g7_ui.c run_command); the status shows the bridge's answer. */
    fun send(command: String) {
        scope.launch {
            val chr = cmdChr ?: return@launch
            try {
                write(chr, command.encodeToByteArray())
                delay(600) // the bridge runs the command off its Bluetooth thread
                refreshStatus()
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _problems.tryEmit(Problem.SendFailed(e.message ?: "?"))
            }
        }
    }

    /**
     * Uploads certificate A, certificate B and the private key as
     * <part 1..3> <offset u16 LE> <up to 16 bytes>, then commits them.
     */
    suspend fun sendCerts(parts: List<ByteArray>, progress: (Int) -> Unit) {
        val chr = certsChr ?: throw IOException("no certificate characteristic")
        val total = parts.sumOf { it.size }
        var sent = 0
        parts.forEachIndexed { i, part ->
            for (off in part.indices step 16) {
                val chunk = part.copyOfRange(off, minOf(off + 16, part.size))
                write(chr, byteArrayOf((i + 1).toByte(), off.toByte(), (off shr 8).toByte()) + chunk)
                sent += chunk.size
                progress(100 * sent / total)
            }
        }
        write(cmdChr ?: throw IOException("disconnected"), "certs_commit".encodeToByteArray())
        delay(600)
        refreshStatus()
    }

    private suspend fun write(chr: BluetoothGattCharacteristic, value: ByteArray) {
        val r = op { g ->
            if (Build.VERSION.SDK_INT >= 33) {
                g.writeCharacteristic(chr, value, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) ==
                    BluetoothGatt.GATT_SUCCESS
            } else {
                @Suppress("DEPRECATION")
                chr.value = value
                chr.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                @Suppress("DEPRECATION")
                g.writeCharacteristic(chr)
            }
        }
        if (r.status != BluetoothGatt.GATT_SUCCESS) throw IOException("GATT status ${r.status}")
    }

    private fun readChr(g: BluetoothGatt, chr: BluetoothGattCharacteristic) = g.readCharacteristic(chr)

    /** Runs one GATT operation and waits for its callback. */
    private suspend fun op(start: (BluetoothGatt) -> Boolean): Result = opLock.withLock {
        val g = gatt ?: throw IOException("disconnected")
        val d = CompletableDeferred<Result>()
        pendingOp = d
        try {
            if (!start(g)) throw IOException("busy")
            withTimeout(10_000) { d.await() }
        } finally {
            pendingOp = null
        }
    }

    private fun finishOp(status: Int, value: ByteArray? = null) {
        pendingOp?.complete(Result(status, value))
    }

    private val callback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g != gatt) return
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                linkUp?.complete(Unit)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                linkUp?.completeExceptionally(IOException("connection failed ($status)"))
                pendingOp?.completeExceptionally(IOException("disconnected"))
                linkDown.complete(Unit)
                wake.trySend(Unit)
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, mtu: Int, status: Int) = finishOp(status)

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) = finishOp(status)

        override fun onCharacteristicRead(
            g: BluetoothGatt, chr: BluetoothGattCharacteristic, value: ByteArray, status: Int,
        ) = finishOp(status, value)

        @Deprecated("Android 12 and older")
        override fun onCharacteristicRead(g: BluetoothGatt, chr: BluetoothGattCharacteristic, status: Int) {
            @Suppress("DEPRECATION")
            finishOp(status, chr.value)
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, chr: BluetoothGattCharacteristic, status: Int) =
            finishOp(status)
    }

    private class DeniedException : Exception()
    private class NotABridgeException : Exception()

    companion object {
        const val NAME = "G7 Bridge"
        private fun uuid(n: Int): UUID = UUID.fromString("a7c1%04x-3f8e-4b5a-9d2c-6e0b1f4a2c90".format(n))
        val SVC = uuid(1)
        val STATUS = uuid(2)
        val CMD = uuid(3)
        val CERTS = uuid(4)
        val DIS: UUID = UUID.fromString("0000180a-0000-1000-8000-00805f9b34fb")
        val FW_REV: UUID = UUID.fromString("00002a26-0000-1000-8000-00805f9b34fb")
    }
}
