package com.bhubit.g7bridge

import android.app.Application
import android.bluetooth.BluetoothDevice
import android.content.Context
import androidx.annotation.StringRes
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.bhubit.g7bridge.ble.BridgeClient
import com.bhubit.g7bridge.ble.BridgeClient.Problem
import com.bhubit.g7bridge.certs.AppCerts
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/** A message for the status line, resolved to text in the UI language. */
data class Msg(@StringRes val id: Int, val args: List<Any> = emptyList())

class MainViewModel(app: Application) : AndroidViewModel(app) {

    val client = BridgeClient(app, viewModelScope)
    private val prefs = app.getSharedPreferences("bridge", Context.MODE_PRIVATE)

    val message = MutableStateFlow<Msg?>(null)
    val latest = MutableStateFlow<ReleaseCheck.Latest?>(null)

    /** Certificates read from a QR code and waiting for "Send to bridge". */
    private val _certs = MutableStateFlow<List<ByteArray>?>(null)
    val certs: StateFlow<List<ByteArray>?> = _certs
    val certsMsg = MutableStateFlow<Msg?>(null)
    val certsBusy = MutableStateFlow(false)

    /** The bridge keeps its last message across connections: show it only as an answer to our command. */
    @Volatile private var awaitingAnswer = false


    /** Bridges for the chooser: bonded ones and the ones found by scanning. */
    val found = MutableStateFlow<List<BluetoothDevice>>(emptyList())

    var savedAddress: String?
        get() = prefs.getString("address", null)
        private set(value) = prefs.edit().putString("address", value).apply()

    init {
        viewModelScope.launch { latest.value = ReleaseCheck.latest() }
        viewModelScope.launch {
            client.problems.collect { p ->
                message.value = when (p) {
                    Problem.Denied -> Msg(R.string.msg_denied)
                    Problem.NotABridge -> Msg(R.string.msg_not_bridge)
                    is Problem.Lost -> Msg(if (p.retrying) R.string.msg_disc_retry else R.string.msg_disc)
                    is Problem.ConnectFailed -> Msg(R.string.msg_no_conn, listOf(p.reason))
                    is Problem.ReadFailed -> Msg(R.string.msg_read_fail, listOf(p.reason))
                    is Problem.SendFailed -> Msg(R.string.msg_send_fail, listOf(p.reason))
                }
            }
        }
        viewModelScope.launch {
            client.conn.collect {
                if (it == BridgeClient.Conn.CONNECTED) message.value = null // the status line says it
                if (it == BridgeClient.Conn.BONDING) message.value = Msg(R.string.msg_bonding)
            }
        }
        viewModelScope.launch {
            client.status.collect { st ->
                if (st == null) return@collect
                if (awaitingAnswer && st.msg.isNotEmpty() && st.msg != "ready") message.value = Msg(R.string.msg_bridge, listOf(st.msg))
            }
        }
    }

    /** Called once the Bluetooth permission is granted. */
    fun start() {
        val saved = savedAddress
        if (saved != null) {
            connect(saved)
        } else {
            client.bondedBridges().singleOrNull()?.let { connect(it.address) }
        }
    }

    /** True the first time: the notification permission is asked for only once. */
    fun askNotificationsOnce(): Boolean {
        if (prefs.getBoolean("asked_notifications", false)) return false
        prefs.edit().putBoolean("asked_notifications", true).apply()
        return true
    }

    fun connect(address: String) {
        savedAddress = address
        client.connect(address)
    }

    fun disconnect() {
        client.disconnect()
        message.value = Msg(R.string.msg_disc)
    }

    fun forgetBridge() {
        client.disconnect()
        prefs.edit().remove("address").apply()
    }

    fun startScan() {
        found.value = client.bondedBridges()
        client.startScan { d ->
            if (found.value.none { it.address == d.address }) found.value = found.value + d
        }
    }

    fun stopScan() = client.stopScan()

    fun setForeground(on: Boolean) {
        client.polling = on
    }

    fun refresh() = viewModelScope.launch { client.refreshStatus() }

    fun sendCode(code: String) {
        message.value = Msg(R.string.msg_sending_code, listOf(code))
        awaitingAnswer = true
        client.send("code $code")
    }

    fun unpairPhones() {
        message.value = Msg(R.string.msg_unpairing)
        awaitingAnswer = true
        client.send("unpair_phone")
    }

    // ---- certificates -----------------------------------------------------------

    private fun takeQrText(text: String) {
        try {
            _certs.value = AppCerts.parse(text)
            certsMsg.value = Msg(R.string.certs_ready)
        } catch (e: AppCerts.Unrecognised) {
            _certs.value = null
            certsMsg.value = Msg(
                when (e.kind) {
                    AppCerts.Unrecognised.Kind.MISSING -> R.string.certs_missing
                    AppCerts.Unrecognised.Kind.TOO_SHORT -> R.string.certs_short
                    AppCerts.Unrecognised.Kind.MALFORMED -> R.string.certs_malformed
                },
            )
        }
    }

    fun certsFromXdripDocs() = viewModelScope.launch {
        certsBusy.value = true
        try {
            takeQrText(AppCerts.fetchXdripQr())
        } catch (e: Exception) {
            certsMsg.value = Msg(R.string.certs_doc_fail, listOf(e.message ?: e.javaClass.simpleName))
        } finally {
            certsBusy.value = false
        }
    }

    fun certsFromCamera(text: String?) {
        if (text != null) takeQrText(text)
    }

    fun sendCerts() = viewModelScope.launch {
        val parts = _certs.value ?: return@launch
        certsBusy.value = true
        awaitingAnswer = true
        try {
            client.sendCerts(parts) { pct -> certsMsg.value = Msg(R.string.certs_sending, listOf(pct)) }
            _certs.value = null
            certsMsg.value = null
        } catch (e: Exception) {
            certsMsg.value = Msg(R.string.msg_send_fail, listOf(e.message ?: "?"))
        } finally {
            certsBusy.value = false
        }
    }

    override fun onCleared() {
        client.stopScan()
        client.disconnect()
    }
}
