package com.bhubit.g7bridge.ui

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.bhubit.g7bridge.AppLanguage
import com.bhubit.g7bridge.BuildConfig
import com.bhubit.g7bridge.MainViewModel
import com.bhubit.g7bridge.Msg
import com.bhubit.g7bridge.R
import com.bhubit.g7bridge.ReleaseCheck
import com.bhubit.g7bridge.ble.BridgeClient.Conn
import com.bhubit.g7bridge.ble.BridgeStatus
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import java.text.DateFormat
import java.util.Date

private val BT_PERMISSIONS =
    if (Build.VERSION.SDK_INT >= 31) {
        arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
    } else {
        arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
    }

@SuppressLint("MissingPermission")
@Composable
fun BridgeScreen(vm: MainViewModel) {
    val ctx = LocalContext.current
    val conn by vm.client.conn.collectAsStateWithLifecycle()
    val st by vm.client.status.collectAsStateWithLifecycle()
    val message by vm.message.collectAsStateWithLifecycle()

    // Permissions and Bluetooth state, checked again whenever the app comes back
    var resumeTick by remember { mutableIntStateOf(0) }
    LifecycleEventEffect(Lifecycle.Event.ON_RESUME) { resumeTick++ }
    val hasPerm = remember(resumeTick) {
        BT_PERMISSIONS.all { ContextCompat.checkSelfPermission(ctx, it) == PackageManager.PERMISSION_GRANTED }
    }
    val btOn = remember(resumeTick) { vm.client.bluetoothOn }
    val permLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        resumeTick++
    }
    val btLauncher = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        resumeTick++
    }
    LaunchedEffect(hasPerm, btOn) {
        if (hasPerm && btOn && conn == Conn.DISCONNECTED) vm.start()
    }
    val notifLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {}
    LaunchedEffect(hasPerm) {
        if (hasPerm && Build.VERSION.SDK_INT >= 33 && vm.askNotificationsOnce()) {
            notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
    }

    var chooser by rememberSaveable { mutableStateOf(false) }

    Scaffold(
        topBar = {
            Surface(color = MaterialTheme.colorScheme.surface) {
                Column(
                    Modifier
                        .statusBarsPadding()
                        .padding(start = 16.dp, end = 12.dp, top = 8.dp, bottom = 8.dp),
                ) {
                    Row(
                        Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineSmall)
                            Spacer(Modifier.height(4.dp))
                            ConnLine(conn)
                        }
                        st?.battery?.let {
                            BatteryCapsule(it)
                            Spacer(Modifier.width(10.dp))
                        }
                        if (hasPerm && btOn) {
                            // A full button, like "Pair": the main action when the link is down
                            if (conn == Conn.DISCONNECTED) {
                                Button(onClick = {
                                    val saved = vm.savedAddress
                                    if (saved != null) vm.connect(saved) else chooser = true
                                }) { Text(stringResource(R.string.btn_connect)) }
                            } else {
                                FilledTonalButton(onClick = vm::disconnect) {
                                    Text(stringResource(R.string.btn_disconnect))
                                }
                            }
                        }
                    }
                }
            }
        },
    ) { pad ->
        Column(
            Modifier
                .padding(pad)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            if (!hasPerm) {
                Notice(stringResource(R.string.perm_text), stringResource(R.string.btn_allow)) {
                    permLauncher.launch(BT_PERMISSIONS)
                }
            } else if (!btOn) {
                Notice(stringResource(R.string.bt_off), stringResource(R.string.btn_bt_on)) {
                    btLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                }
            }
            message?.let {
                Text(msgText(it), style = MaterialTheme.typography.bodyMedium)
            }

            ReadingCard(st, conn == Conn.CONNECTED)
            RangeCard(st)
            SensorCard(st)
            PairCard(vm, st, conn == Conn.CONNECTED)
            CertsCard(vm, st, conn == Conn.CONNECTED)
            HelpCard(vm, conn == Conn.CONNECTED) { chooser = true }
            // Versions (each with its update check) and the language switch at the very bottom
            Row(Modifier.padding(horizontal = 4.dp), verticalAlignment = Alignment.Top) {
                Box(Modifier.weight(1f)) { VersionLines(vm, st) }
                LanguageSwitch()
            }
            Spacer(Modifier.height(24.dp))
        }
    }

    if (chooser) {
        ChooserDialog(vm, onDismiss = { chooser = false })
    }
}

// ---- small building blocks ---------------------------------------------------------

@Composable
private fun msgText(m: Msg): String = stringResource(m.id, *m.args.toTypedArray())

@Composable
private fun ConnLine(conn: Conn) {
    val (text, color) = when (conn) {
        Conn.CONNECTED -> stringResource(R.string.conn_yes) to Tone.Good
        Conn.CONNECTING -> stringResource(R.string.conn_connecting) to Tone.Warn
        Conn.BONDING -> stringResource(R.string.conn_bonding) to Tone.Warn
        Conn.DISCONNECTED -> stringResource(R.string.conn_no) to MaterialTheme.colorScheme.outline
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(16.dp).background(color, CircleShape))
        Spacer(Modifier.width(10.dp))
        Text(text, style = MaterialTheme.typography.titleMedium)
    }
}

@Composable
private fun Notice(text: String, button: String, onClick: () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Text(text)
            Spacer(Modifier.height(8.dp))
            Button(onClick = onClick) { Text(button) }
        }
    }
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

@Composable
private fun Field(label: String, value: @Composable () -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(
            label,
            Modifier.width(120.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Box(Modifier.weight(1f)) { value() }
    }
}

@Composable
private fun Field(label: String, value: String) = Field(label) { Text(value) }

@Composable
private fun Pill(text: String, color: Color? = null, onClick: (() -> Unit)? = null) {
    val c = color ?: MaterialTheme.colorScheme.outline
    Text(
        text,
        Modifier
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .background(c.copy(alpha = 0.15f), RoundedCornerShape(50))
            .padding(horizontal = 8.dp, vertical = 2.dp),
        color = if (color == null) MaterialTheme.colorScheme.onSurfaceVariant else c,
        style = MaterialTheme.typography.labelMedium,
    )
}

@Composable
private fun Bars(rssi: Int) {
    val n = if (rssi > -65) 3 else if (rssi > -80) 2 else 1
    Row(verticalAlignment = Alignment.Bottom, horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        listOf(5, 9, 14).forEachIndexed { i, h ->
            Box(
                Modifier
                    .width(4.dp)
                    .height(h.dp)
                    .background(
                        if (i < n) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant,
                        RoundedCornerShape(1.dp),
                    ),
            )
        }
    }
}

@Composable
private fun RssiValue(rssi: Int, extra: String? = null) {
    val (label, tone) = when {
        rssi >= -70 -> stringResource(R.string.rssi_good) to Tone.Good
        rssi >= -85 -> stringResource(R.string.rssi_mid) to Tone.Warn
        else -> stringResource(R.string.rssi_bad) to Tone.Bad
    }
    FlowRowCentered {
        Text("$rssi dBm")
        Bars(rssi)
        Pill(label, tone)
        extra?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun FlowRowCentered(content: @Composable () -> Unit) {
    FlowRow(
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
        itemVerticalAlignment = Alignment.CenterVertically,
    ) { content() }
}

@Composable
private fun ago(s: Int): String = when {
    s < 90 -> stringResource(R.string.ago_s, s)
    s < 5400 -> stringResource(R.string.ago_min, Math.round(s / 60.0).toInt())
    s < 172800 -> stringResource(R.string.ago_h, Math.round(s / 3600.0).toInt())
    else -> stringResource(R.string.ago_d, Math.round(s / 86400.0).toInt())
}

private const val G7_SESSION_MIN = 15120 // 10 days + 12 h grace

private fun num(v: Double, digits: Int = 1) = "%.${digits}f".format(v)

// ---- cards -------------------------------------------------------------------------

@Composable
private fun stateInfo(s: Int): Pair<String, Color> = when (s) {
    0x06 -> stringResource(R.string.st_ok) to Tone.Good
    0x07 -> stringResource(R.string.st_cal) to Tone.Warn
    0x02 -> stringResource(R.string.st_warm) to Tone.Warn
    0x01, 0x0f, 0x18, 0x1a, 0xc2 -> stringResource(R.string.st_end) to Tone.Bad
    0x03 -> stringResource(R.string.st_noise) to Tone.Warn
    else -> stringResource(R.string.st_fail, s) to Tone.Bad
}

@Composable
private fun trendText(tenths: Int?): String {
    if (tenths == null) return stringResource(R.string.trend_unknown)
    val v = tenths / 10.0
    val arrow = when {
        v <= -2 -> "↓↓"
        v <= -1 -> "↓"
        v < -0.5 -> "↘"
        v <= 0.5 -> "→"
        v < 1 -> "↗"
        v < 2 -> "↑"
        else -> "↑↑"
    }
    return stringResource(R.string.trend_value, arrow, (if (v > 0) "+" else "") + num(v))
}

@Composable
private fun ReadingCard(st: BridgeStatus?, connected: Boolean) {
    val g = st?.reading
    Section(stringResource(R.string.h_reading)) {
        val color = when {
            g == null || !g.usable -> MaterialTheme.colorScheme.outline
            g.mgdl < 70 -> Tone.Bad
            g.mgdl > 180 -> Tone.Warn
            else -> Tone.Good
        }
        Row(verticalAlignment = Alignment.Bottom) {
            Text(g?.mgdl?.toString() ?: "—", fontSize = 56.sp, fontWeight = FontWeight.Bold, color = color)
            Spacer(Modifier.width(6.dp))
            Text(stringResource(R.string.mgdl), Modifier.padding(bottom = 10.dp))
        }
        when {
            g != null -> {
                Text(if (g.usable) trendText(g.trendTenths) else stringResource(R.string.not_forwarded))
                val (label, tone) = stateInfo(g.state)
                FlowRowCentered {
                    Pill(label, tone)
                    Text(ago(g.ageS), style = MaterialTheme.typography.bodySmall)
                }
            }
            st != null -> {
                Text(stringResource(R.string.no_reading))
                Pill(stringResource(R.string.wait_sensor))
            }
            else -> Text(stringResource(R.string.trend_init))
        }
        if (!connected && st != null) {
            Text(
                stringResource(R.string.conn_no),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.outline,
            )
        }
    }
}

@Composable
private fun SensorCard(st: BridgeStatus?) {
    Section(stringResource(R.string.h_sensor)) {
        val using = st?.inUse
        Field(
            stringResource(R.string.s_name),
            using?.name ?: st?.id?.takeIf { it.isNotEmpty() }?.let { "DXCM$it" }
                ?: stringResource(R.string.not_selected),
        )
        val sess = st?.session
        val min = sess?.minutes
        val len = sess?.lengthMin
        Field(stringResource(R.string.s_day)) {
            Column {
                Text(
                    when {
                        min != null && len != null && len > 0 ->
                            // whole days, e.g. "day 16 of 10"
                            stringResource(R.string.day_of, (min / 1440).toString(), (len / 1440).toString())
                        min != null -> stringResource(R.string.day, (min / 1440).toString())
                        else -> stringResource(R.string.sess_unknown)
                    },
                )
                if (min != null && len != null && len > 0) {
                    LinearProgressIndicator(
                        progress = { (min.toFloat() / len).coerceIn(0f, 1f) },
                        Modifier.fillMaxWidth().padding(top = 4.dp),
                    )
                }
            }
        }
        // From the sensor's own clock (no session start from xDrip+ needed). A sensor that did not
        // tell its session length (often one that has ended) gets the G7 standard, marked "~".
        Field(
            stringResource(R.string.s_end),
            if (min != null) {
                val end = Date(System.currentTimeMillis() + ((len ?: G7_SESSION_MIN) - min) * 60_000L)
                (if (len == null) "~ " else "") +
                    DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(end) +
                    if (end.before(Date())) " " + stringResource(R.string.passed) else ""
            } else {
                "—"
            },
        )
        Field(
            stringResource(R.string.s_code),
            st?.code?.takeIf { it.isNotEmpty() } ?: stringResource(R.string.code_not_set),
        )
        Field(
            stringResource(R.string.s_phone),
            if (st == null) "—" else stringResource(if (st.xdrip ?: st.phone) R.string.phone_on else R.string.phone_off),
        )
    }
}

/** PL · EN: the current language stands out; the other one switches to it. */
@Composable
private fun LanguageSwitch() {
    val ctx = LocalContext.current
    val current = LocalConfiguration.current.locales[0].language
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 6.dp)) {
        listOf("pl" to "PL", "en" to "EN").forEachIndexed { i, (tag, label) ->
            if (i > 0) Text(" · ", color = MaterialTheme.colorScheme.outline)
            val active = current == tag || (tag == "en" && current != "pl")
            Text(
                label,
                Modifier
                    .clickable(enabled = !active) { ctx.findActivity()?.let { AppLanguage.set(it, tag) } }
                    .padding(horizontal = 6.dp, vertical = 4.dp),
                color = if (active) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                fontWeight = if (active) FontWeight.Bold else FontWeight.Normal,
                style = MaterialTheme.typography.labelLarge,
            )
        }
    }
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

/** The bridge's firmware and this app, each with its latest-release check. */
@Composable
private fun VersionLines(vm: MainViewModel, st: BridgeStatus?) {
    val latest by vm.latest.collectAsStateWithLifecycle()
    val latestApp by vm.latestApp.collectAsStateWithLifecycle()
    val disFw by vm.client.fwRevision.collectAsStateWithLifecycle()
    val pl = LocalConfiguration.current.locales[0].language == "pl"
    Column(Modifier.padding(top = 6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        (st?.fw ?: disFw)?.let { fw ->
            VersionLine(
                stringResource(R.string.fw_line, fw), fw, latest,
                current = R.string.fw_current, updateText = R.string.fw_update_text, howUrl = ReleaseCheck.HOWTO,
            )
        }
        VersionLine(
            stringResource(R.string.app_version, BuildConfig.VERSION_NAME), BuildConfig.VERSION_NAME, latestApp,
            current = R.string.app_current, updateText = R.string.app_update_text,
            howUrl = if (pl) ReleaseCheck.APP_HOWTO_PL else ReleaseCheck.APP_HOWTO,
        )
    }
}

@Composable
private fun VersionLine(
    label: String,
    installed: String,
    latest: ReleaseCheck.Latest?,
    current: Int,
    updateText: Int,
    howUrl: String,
) {
    val uri = LocalUriHandler.current
    var updateDialog by remember { mutableStateOf(false) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(6.dp))
        when (ReleaseCheck.compare(installed, latest)) {
            ReleaseCheck.Verdict.UPDATE ->
                Pill(stringResource(R.string.fw_new, latest!!.version), Tone.Warn) { updateDialog = true }
            ReleaseCheck.Verdict.DEV_BUILD -> Pill(stringResource(R.string.fw_dev))
            ReleaseCheck.Verdict.CURRENT -> Pill(stringResource(current), Tone.Good)
            ReleaseCheck.Verdict.UNKNOWN -> {}
        }
    }
    val l = latest
    if (updateDialog && l != null) {
        AlertDialog(
            onDismissRequest = { updateDialog = false },
            title = { Text(stringResource(R.string.fw_new, l.version)) },
            text = { Text(stringResource(updateText)) },
            confirmButton = {
                TextButton(onClick = {
                    updateDialog = false
                    uri.openUri(l.downloadUrl)
                }) { Text(stringResource(R.string.fw_get)) }
            },
            dismissButton = {
                TextButton(onClick = {
                    updateDialog = false
                    uri.openUri(howUrl)
                }) { Text(stringResource(R.string.fw_how)) }
            },
        )
    }
}

/** The bridge's battery like a phone's: the fill shrinks and changes colour; a bolt while charging. */
@Composable
private fun BatteryCapsule(bat: BridgeStatus.Battery) {
    val pct = bat.pct.coerceIn(0, 100)
    val fill = when {
        pct <= 20 -> Tone.Bad
        pct <= 30 -> Tone.Warn
        else -> Tone.Good
    }
    val frame = MaterialTheme.colorScheme.onSurfaceVariant
    val ink = MaterialTheme.colorScheme.onSurface
    val desc = stringResource(if (bat.usb) R.string.bat_charging else R.string.bat_desc, pct)
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier.semantics { contentDescription = desc },
    ) {
        Box(
            Modifier
                .size(width = 56.dp, height = 28.dp)
                .border(2.dp, frame, RoundedCornerShape(8.dp))
                .padding(4.dp),
        ) {
            Box(
                Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(pct / 100f)
                    .background(fill, RoundedCornerShape(4.dp)),
            )
            if (bat.usb) {
                Bolt(ink, Modifier.align(Alignment.Center).size(width = 12.dp, height = 18.dp))
            } else {
                Text(
                    "$pct%",
                    Modifier.align(Alignment.Center),
                    color = ink,
                    fontWeight = FontWeight.Bold,
                    style = MaterialTheme.typography.labelMedium,
                )
            }
        }
        Box(
            Modifier
                .size(width = 3.dp, height = 10.dp)
                .background(frame, RoundedCornerShape(topEnd = 2.dp, bottomEnd = 2.dp)),
        )
    }
}

@Composable
private fun Bolt(color: Color, modifier: Modifier) {
    Canvas(modifier) {
        val w = size.width
        val h = size.height
        val bolt = Path().apply {
            moveTo(0.62f * w, 0f)
            lineTo(0.05f * w, 0.58f * h)
            lineTo(0.45f * w, 0.58f * h)
            lineTo(0.35f * w, h)
            lineTo(0.95f * w, 0.4f * h)
            lineTo(0.55f * w, 0.4f * h)
            close()
        }
        drawPath(bolt, color)
    }
}

@Composable
private fun RangeCard(st: BridgeStatus?) {
    Section(stringResource(R.string.h_range)) {
        Field(stringResource(R.string.p_rssi)) { st?.phoneRssi?.let { RssiValue(it) } ?: Text("—") }
        Field(stringResource(R.string.s_rssi)) {
            st?.inUse?.let { RssiValue(it.rssi, stringResource(R.string.seen_ago, ago(it.agoS))) } ?: Text("—")
        }
        Text(stringResource(R.string.range_hint), style = MaterialTheme.typography.bodySmall)
    }
}

@Composable
private fun PairCard(vm: MainViewModel, st: BridgeStatus?, connected: Boolean) {
    var code by rememberSaveable { mutableStateOf("") }
    val valid = Regex("^(\\d{4}|\\d{6})$").matches(code)
    Section(stringResource(R.string.h_pair)) {
        Text(stringResource(R.string.pair_hint), style = MaterialTheme.typography.bodySmall)
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                value = code,
                onValueChange = { v -> code = v.filter { it.isDigit() }.take(6) },
                label = { Text(stringResource(R.string.code_label)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword, imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = { if (valid && connected) vm.sendCode(code) }),
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(8.dp))
            Button(onClick = { vm.sendCode(code) }, enabled = valid && connected) {
                Text(stringResource(R.string.pair_btn))
            }
        }
        if (st?.searching == true) {
            Pill(stringResource(R.string.searching, st.code, (st.searchLeftS + 59) / 60), Tone.Warn)
        }

        HorizontalDivider(Modifier.padding(vertical = 4.dp))
        Text(stringResource(R.string.h_seen), style = MaterialTheme.typography.titleSmall)
        val seen = st?.seen.orEmpty().sortedWith(compareByDescending<BridgeStatus.SeenSensor> { it.use }.thenByDescending { it.rssi })
        if (seen.isEmpty()) {
            Text(stringResource(R.string.seen_none), style = MaterialTheme.typography.bodySmall)
        }
        seen.forEach { s ->
            Column {
                FlowRowCentered {
                    Text(s.name, fontWeight = FontWeight.SemiBold)
                    Bars(s.rssi)
                    when {
                        s.use -> Pill(stringResource(R.string.pill_used), Tone.Good)
                        s.tried -> Pill(stringResource(R.string.pill_nofit))
                        st?.searching == true -> Pill(stringResource(R.string.pill_check), Tone.Warn)
                        else -> Pill(stringResource(R.string.pill_other))
                    }
                }
                Text(stringResource(R.string.seen_ago, ago(s.agoS)), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CertsCard(vm: MainViewModel, st: BridgeStatus?, connected: Boolean) {
    val canLoad by vm.client.canLoadCerts.collectAsStateWithLifecycle()
    val ready by vm.certs.collectAsStateWithLifecycle()
    val busy by vm.certsBusy.collectAsStateWithLifecycle()
    val msg by vm.certsMsg.collectAsStateWithLifecycle()
    val camPrompt = stringResource(R.string.cam_prompt)
    val scan = rememberLauncherForActivityResult(ScanContract()) { vm.certsFromCamera(it.contents) }
    Section(stringResource(R.string.h_certs)) {
        when {
            st == null -> Text("—")
            !canLoad || st.certs == null -> Pill(stringResource(R.string.certs_old), Tone.Warn)
            st.certs == "none" -> Pill(stringResource(R.string.certs_none), Tone.Bad)
            st.certs == "stored" -> Pill(stringResource(R.string.certs_stored), Tone.Good)
            else -> Pill(stringResource(R.string.certs_builtin), Tone.Good)
        }
        Expandable(stringResource(R.string.certs_what)) {
            Text(stringResource(R.string.certs_hint), style = MaterialTheme.typography.bodySmall)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { vm.certsFromXdripDocs() }, enabled = !busy) {
                Text(stringResource(R.string.certs_doc))
            }
            OutlinedButton(
                onClick = {
                    scan.launch(
                        ScanOptions()
                            .setDesiredBarcodeFormats(ScanOptions.QR_CODE)
                            .setPrompt(camPrompt)
                            .setBeepEnabled(false)
                            .setOrientationLocked(false),
                    )
                },
                enabled = !busy,
            ) { Text(stringResource(R.string.certs_cam)) }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = { vm.sendCerts() }, enabled = ready != null && connected && canLoad && !busy) {
                Text(stringResource(R.string.certs_send))
            }
            if (busy) {
                Spacer(Modifier.width(12.dp))
                CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp)
            }
        }
        msg?.let { Text(msgText(it), style = MaterialTheme.typography.bodySmall) }
    }
}

@Composable
private fun Expandable(title: String, content: @Composable () -> Unit) {
    var open by rememberSaveable(title) { mutableStateOf(false) }
    Column {
        Text(
            (if (open) "▾ " else "▸ ") + title,
            Modifier
                .fillMaxWidth()
                .clickable { open = !open }
                .padding(vertical = 8.dp),
            style = MaterialTheme.typography.titleSmall,
        )
        if (open) {
            Column(Modifier.padding(start = 16.dp, bottom = 8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                content()
            }
        }
    }
}

@Composable
private fun HelpCard(vm: MainViewModel, connected: Boolean, chooseBridge: () -> Unit) {
    var confirm by remember { mutableStateOf(false) }
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
            Expandable(stringResource(R.string.xd_title)) {
                listOf(R.string.xd1, R.string.xd2, R.string.xd3, R.string.xd4, R.string.xd5, R.string.xd6)
                    .forEachIndexed { i, id -> Text("${i + 1}. " + stringResource(id)) }
            }
            HorizontalDivider()
            Expandable(stringResource(R.string.led_title)) {
                listOf(
                    R.string.led_g1 to R.string.led_g1d,
                    R.string.led_g2 to R.string.led_g2d,
                    R.string.led_gb3 to R.string.led_gb3d,
                    R.string.led_gb1 to R.string.led_gb1d,
                    R.string.led_bb to R.string.led_bbd,
                    R.string.led_b5 to R.string.led_b5d,
                    R.string.led_r to R.string.led_rd,
                ).forEach { (what, means) ->
                    Column {
                        Text(stringResource(what), fontWeight = FontWeight.SemiBold)
                        Text(stringResource(means), style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
            HorizontalDivider()
            Expandable(stringResource(R.string.unp_title)) {
                Text(stringResource(R.string.unp_text), style = MaterialTheme.typography.bodySmall)
                OutlinedButton(onClick = { confirm = true }, enabled = connected) {
                    Text(stringResource(R.string.unp_btn))
                }
                TextButton(onClick = {
                    vm.forgetBridge()
                    chooseBridge()
                }) { Text(stringResource(R.string.forget_btn)) }
            }
            HorizontalDivider()
            Expandable(stringResource(R.string.h_about)) {
                Text(stringResource(R.string.tagline), fontWeight = FontWeight.SemiBold)
                Text(stringResource(R.string.about1))
                Text(stringResource(R.string.about2))
                Text(stringResource(R.string.about3))
                Text(stringResource(R.string.disclaimer), style = MaterialTheme.typography.bodySmall)
            }
        }
    }
    if (confirm) {
        AlertDialog(
            onDismissRequest = { confirm = false },
            title = { Text(stringResource(R.string.unp_title)) },
            text = { Text(stringResource(R.string.unp_text)) },
            confirmButton = {
                TextButton(onClick = {
                    confirm = false
                    vm.unpairPhones()
                }) { Text(stringResource(R.string.unp_yes)) }
            },
            dismissButton = {
                TextButton(onClick = { confirm = false }) { Text(stringResource(R.string.btn_cancel)) }
            },
        )
    }
}

@SuppressLint("MissingPermission")
@Composable
private fun ChooserDialog(vm: MainViewModel, onDismiss: () -> Unit) {
    val found by vm.found.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { vm.startScan() }
    val close = {
        vm.stopScan()
        onDismiss()
    }
    AlertDialog(
        onDismissRequest = close,
        title = { Text(stringResource(R.string.choose_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(stringResource(R.string.choose_hint), style = MaterialTheme.typography.bodySmall)
                if (found.isEmpty()) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(Modifier.size(18.dp), strokeWidth = 2.dp)
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.choose_scanning))
                    }
                }
                found.forEach { d ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clickable {
                                vm.connect(d.address)
                                close()
                            }
                            .padding(vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(d.name ?: "G7 Bridge", fontWeight = FontWeight.SemiBold)
                            Text(d.address, style = MaterialTheme.typography.bodySmall)
                        }
                        if (d.bondState == BluetoothDevice.BOND_BONDED) {
                            Pill(stringResource(R.string.choose_bonded), Tone.Good)
                        }
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = { TextButton(onClick = close) { Text(stringResource(R.string.btn_cancel)) } },
    )
}
