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
import com.bhubit.g7bridge.watch.NotificationLog
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** A message for the status line, resolved to text in the UI language. */
data class Msg(@StringRes val id: Int, val args: List<Any> = emptyList())

class MainViewModel(app: Application) : AndroidViewModel(app) {

    val client = BridgeClient(app, viewModelScope)
    private val prefs = app.getSharedPreferences("bridge", Context.MODE_PRIVATE)

    val message = MutableStateFlow<Msg?>(null)
    val latest = MutableStateFlow<ReleaseCheck.Latest?>(null)
    val latestApp = MutableStateFlow<ReleaseCheck.Latest?>(null)
    /** Declared before init: the first check runs from init and sets it. */
    @Volatile private var releasesChecked = 0L

    val notifications = NotificationLog.load(app)
    val notificationsOpen = MutableStateFlow(false)

    /** Opens the notifications screen; [entry] = the one whose system notification was tapped. */
    fun openNotifications(entry: Long?) {
        entry?.let { NotificationLog.markRead(getApplication(), it) }
        notificationsOpen.value = true
    }

    fun closeNotifications() {
        notificationsOpen.value = false
    }

    fun markNotificationRead(id: Long) = NotificationLog.markRead(getApplication(), id)
    fun markAllNotificationsRead() = NotificationLog.markAllRead(getApplication())
    fun clearNotifications() = NotificationLog.clear(getApplication())

    /** Certificates read from a QR code and waiting for "Send to bridge". */
    private val _certs = MutableStateFlow<List<ByteArray>?>(null)
    val certs: StateFlow<List<ByteArray>?> = _certs
    val certsMsg = MutableStateFlow<Msg?>(null)
    val certsBusy = MutableStateFlow(false)

    /** The bridge keeps its last message across connections: show it only as an answer to our command. */
    @Volatile private var awaitingAnswer = false
    private var sentAt = 0L
    private var msgBefore: String? = null

    private fun expectAnswer() {
        sentAt = System.currentTimeMillis()
        msgBefore = client.status.value?.msg
        awaitingAnswer = true
    }

    /** The bridge answers in English; known answers get the UI language. */
    private fun answer(raw: String): Msg {
        for ((re, id) in ANSWERS) {
            re.matchEntire(raw)?.let { m -> return Msg(id, m.groupValues.drop(1)) }
        }
        return Msg(R.string.msg_bridge, listOf(raw))
    }


    /**
     * The bridge takes one phone link and stops advertising while it has one, so xDrip+ cannot find
     * it in its scan while this app holds the link. If xDrip+ does not collect yet, the link is
     * released in the background and taken again on return.
     */
    private var releaseJob: Job? = null
    @Volatile private var releasedForXdrip = false
    /** Set when this app bonded the bridge: the xDrip+ hint stays until xDrip+ collects. */
    @Volatile private var bondedHere = false

    /** Bridges for the chooser: bonded ones and the ones found by scanning. */
    val found = MutableStateFlow<List<BluetoothDevice>>(emptyList())

    var savedAddress: String?
        get() = prefs.getString("address", null)
        private set(value) = prefs.edit().putString("address", value).apply()

    init {
        // New releases: on start, every 30 min while on screen, and on return after 5 min
        viewModelScope.launch {
            while (true) {
                if (client.polling) checkReleases()
                delay(30 * 60_000L)
            }
        }
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
                if (it == BridgeClient.Conn.CONNECTED) {
                    // the status line says it; after a fresh pairing xDrip+ still has to be added
                    // (the bridge drops the link once after pairing, so this comes twice)
                    message.value = if (bondedHere) Msg(R.string.msg_add_xdrip) else null
                }
                if (it == BridgeClient.Conn.BONDING) {
                    bondedHere = true
                    message.value = Msg(R.string.msg_bonding)
                }
            }
        }
        viewModelScope.launch {
            client.status.collect { st ->
                if (st == null) return@collect
                if (bondedHere && st.xdrip != false) {
                    bondedHere = false
                    if (message.value == Msg(R.string.msg_add_xdrip)) message.value = null
                }
                // A repeated command gives the same answer as before: then take it after 2 s
                if (awaitingAnswer && st.msg.isNotEmpty() && st.msg != "ready" &&
                    (st.msg != msgBefore || System.currentTimeMillis() - sentAt > 2_000)
                ) {
                    awaitingAnswer = false
                    val m = answer(st.msg)
                    message.value = m
                    // Only a confirmation: the sections show the state itself
                    viewModelScope.launch {
                        delay(20_000)
                        if (message.value == m) message.value = null
                    }
                }
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
        releaseJob?.cancel()
        if (on) {
            if (releasedForXdrip) {
                releasedForXdrip = false
                savedAddress?.let { client.connect(it) }
            }
        } else {
            releaseJob = viewModelScope.launch {
                delay(3_000) // not for a quick look elsewhere
                val st = client.status.value
                if (client.conn.value == BridgeClient.Conn.CONNECTED && st?.xdrip == false && !certsBusy.value) {
                    releasedForXdrip = true
                    client.disconnect()
                }
            }
        }
        if (on && System.currentTimeMillis() - releasesChecked > 5 * 60_000L) {
            viewModelScope.launch { checkReleases() }
        }
    }

    private suspend fun checkReleases() {
        releasesChecked = System.currentTimeMillis()
        ReleaseCheck.releases()?.let {
            latest.value = it.firmware
            latestApp.value = it.app
        }
    }

    fun refresh() = viewModelScope.launch { client.refreshStatus() }

    fun sendCode(code: String) {
        message.value = Msg(R.string.msg_sending_code, listOf(code))
        expectAnswer()
        client.send("code $code")
    }

    /** The bridge lets go of the sensor (for xDrip+ or the Dexcom app) until a code is set again. */
    fun releaseSensor() {
        message.value = Msg(R.string.msg_releasing)
        expectAnswer()
        client.send("release")
    }

    fun unpairPhones() {
        message.value = Msg(R.string.msg_unpairing)
        expectAnswer()
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
        expectAnswer()
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

/** The bridge's answers (firmware g7_ui.c), most specific first. */
private val ANSWERS = listOf(
    Regex("""^code (\d+) set, searching$""") to R.string.ans_code,
    Regex("""^invalid code$""") to R.string.ans_invalid_code,
    Regex("""^set a code first$""") to R.string.ans_set_code_first,
    Regex("""^searching$""") to R.string.ans_searching,
    Regex("""^sensor id set$""") to R.string.ans_id_set,
    Regex("""^invalid id$""") to R.string.ans_invalid_id,
    Regex("""^sensor released$""") to R.string.ans_released,
    Regex("""^(\d+) phone bond\(s\) removed.*$""") to R.string.ans_unpaired,
    Regex("""^G7 configuration saved$""") to R.string.ans_cfg_saved,
    Regex("""^G7 configuration: QR code data does not match$""") to R.string.ans_cfg_mismatch,
    Regex("""^G7 configuration: upload incomplete$""") to R.string.ans_cfg_incomplete,
    Regex("""^G7 configuration: saving failed$""") to R.string.ans_cfg_failed,
    Regex("""^G7 configuration removed$""") to R.string.ans_cfg_removed,
    Regex("""^unknown command$""") to R.string.ans_unknown,
)
