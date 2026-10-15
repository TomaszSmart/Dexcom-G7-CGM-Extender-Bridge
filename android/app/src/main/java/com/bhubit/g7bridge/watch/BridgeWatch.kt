package com.bhubit.g7bridge.watch

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.text.format.DateFormat
import androidx.core.content.ContextCompat
import androidx.core.content.edit
import androidx.work.CoroutineWorker
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import com.bhubit.g7bridge.AppLanguage
import com.bhubit.g7bridge.R
import com.bhubit.g7bridge.ReleaseCheck
import com.bhubit.g7bridge.ble.BridgeClient
import com.bhubit.g7bridge.ble.BridgeStatus
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeout
import java.util.Date
import java.util.concurrent.TimeUnit

/**
 * Every ~15 minutes (Android's shortest periodic job, also run in battery saver) the app
 * reads the bridge's status once and notifies about what xDrip+ cannot see. No service
 * stays running: xDrip+ already keeps the Bluetooth link to the bridge open.
 */
class BridgeCheckWorker(ctx: Context, params: WorkerParameters) : CoroutineWorker(ctx, params) {

    override suspend fun doWork(): Result {
        val ctx = AppLanguage.wrap(applicationContext)
        val address = ctx.getSharedPreferences("bridge", Context.MODE_PRIVATE).getString("address", null)
            ?: return Result.success()
        if (Build.VERSION.SDK_INT >= 31 &&
            ContextCompat.checkSelfPermission(ctx, Manifest.permission.BLUETOOTH_CONNECT) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            return Result.success()
        }
        BridgeWatch(ctx).evaluate(read(address))
        return Result.success()
    }

    /** Two tries: the first may hit the moment the bridge is busy with the sensor. */
    private suspend fun read(address: String): BridgeStatus? {
        repeat(2) { attempt ->
            try {
                return withTimeout(45_000) {
                    coroutineScope { BridgeClient(applicationContext, this).readStatusOnce(address) }
                }
            } catch (e: CancellationException) {
                if (isStopped) throw e
            } catch (e: Exception) {
                // retried below
            }
            if (attempt == 0) delay(15_000)
        }
        return null
    }
}

class BridgeWatch(private val ctx: Context) {
    private val prefs = ctx.getSharedPreferences("watch", Context.MODE_PRIVATE)

    suspend fun evaluate(st: BridgeStatus?) {
        if (st == null) {
            unreachable()
            return
        }
        prefs.edit {
            putInt("fails", 0)
            if (st.xdrip != false) putBoolean("xdrip_missing", false)
            putLong("seen_ms", System.currentTimeMillis())
            st.battery?.let { putInt("last_bat", it.pct) }
        }
        battery(st)
        certs(st)
        diagnose(st)
        firmware(st)
    }

    // ---- 1. the bridge's battery ---------------------------------------------------

    private fun battery(st: BridgeStatus) {
        val b = st.battery ?: return
        val notified = prefs.getInt("bat_level", 100) // the last threshold we notified about
        when {
            b.usb || b.pct >= 30 -> if (notified != 100) {
                prefs.edit { putInt("bat_level", 100) }
                Alerts.cancel(ctx, Alerts.ID_BATTERY)
            }
            b.pct <= 10 && notified > 10 -> {
                Alerts.batteryLow(ctx, b.pct, empty = true)
                prefs.edit { putInt("bat_level", 10) }
            }
            b.pct <= 20 && notified > 20 -> {
                Alerts.batteryLow(ctx, b.pct, empty = false)
                prefs.edit { putInt("bat_level", 20) }
            }
        }
    }

    // ---- 2. why no data arrives -------------------------------------------------------

    private fun unreachable() {
        val fails = prefs.getInt("fails", 0) + 1
        prefs.edit { putInt("fails", fails) }
        if (fails < 2) return // ~30 min without contact before we say so
        val seen = prefs.getLong("seen_ms", 0)
        val bat = prefs.getInt("last_bat", -1)
        val time = if (seen > 0) DateFormat.getTimeFormat(ctx).format(Date(seen)) else "—"
        showDiagnosis(
            "unreachable",
            ctx.getString(R.string.n_unreach_title),
            if (bat >= 0) ctx.getString(R.string.n_unreach_text_bat, time, bat)
            else ctx.getString(R.string.n_unreach_text, time),
        )
    }

    private fun diagnose(st: BridgeStatus) {
        val g = st.reading
        val name = st.inUse?.name ?: st.id.takeIf { it.isNotEmpty() }?.let { "DXCM$it" } ?: "G7"
        when {
            st.searching -> clearDiagnosis() // a new sensor is being looked for: expected
            // The sensor itself reports the end of its session or a failure
            g != null && g.state !in OK_STATES -> if (g.state in END_STATES) {
                showDiagnosis(
                    "end:$name", ctx.getString(R.string.n_end_title), ctx.getString(R.string.n_end_text, name),
                )
            } else {
                showDiagnosis(
                    "fail:$name:${g.state}",
                    ctx.getString(R.string.n_fail_title, g.state),
                    ctx.getString(R.string.n_fail_text, name),
                )
            }
            g != null && g.ageS <= STALE_S -> if (st.xdrip == false && xdripMissingTwice()) {
                showDiagnosis(
                    "noxdrip", ctx.getString(R.string.n_noxdrip_title), ctx.getString(R.string.n_noxdrip_text),
                )
            } else if (st.xdrip != false) {
                clearDiagnosis()
            }
            st.code.isEmpty() && st.id.isEmpty() -> clearDiagnosis() // no sensor set up yet
            st.inUse == null -> showDiagnosis(
                "nosensor", ctx.getString(R.string.n_nosensor_title), ctx.getString(R.string.n_nosensor_text),
            )
            st.inUse!!.agoS > STALE_S -> showDiagnosis(
                "nosensor",
                ctx.getString(R.string.n_nosensor_title),
                ctx.getString(R.string.n_nosensor_text_min, st.inUse!!.agoS / 60),
            )
            else -> showDiagnosis(
                "noreading", ctx.getString(R.string.n_noreading_title), ctx.getString(R.string.n_noreading_text),
            )
        }
    }

    /** xDrip+ reconnects within seconds; only two checks in a row (~30 min) count. */
    private fun xdripMissingTwice(): Boolean {
        val before = prefs.getBoolean("xdrip_missing", false)
        prefs.edit { putBoolean("xdrip_missing", true) }
        return before
    }

    private fun showDiagnosis(key: String, title: String, text: String) {
        if (prefs.getString("diag", null) == key) return // already shown
        prefs.edit { putString("diag", key) }
        Alerts.diagnosis(ctx, title, text)
    }

    private fun clearDiagnosis() {
        if (prefs.getString("diag", null) == null) return
        prefs.edit { remove("diag") }
        Alerts.cancel(ctx, Alerts.ID_DIAGNOSIS)
    }

    // ---- 3. certificates and firmware updates ------------------------------------------

    private fun certs(st: BridgeStatus) {
        val missing = st.certs == "none"
        val notified = prefs.getBoolean("certs_notified", false)
        if (missing && !notified) {
            Alerts.certsMissing(ctx)
            prefs.edit { putBoolean("certs_notified", true) }
        } else if (!missing && notified) {
            prefs.edit { putBoolean("certs_notified", false) }
            Alerts.cancel(ctx, Alerts.ID_CERTS)
        }
    }

    /** Asks GitHub at most once a day. */
    private suspend fun firmware(st: BridgeStatus) {
        val now = System.currentTimeMillis()
        if (now - prefs.getLong("fw_checked_ms", 0) < TimeUnit.DAYS.toMillis(1)) return
        val latest = ReleaseCheck.latest() ?: return
        prefs.edit { putLong("fw_checked_ms", now) }
        if (ReleaseCheck.compare(st.fw, latest) == ReleaseCheck.Verdict.UPDATE &&
            prefs.getString("fw_notified", null) != latest.version
        ) {
            Alerts.firmwareUpdate(ctx, latest.version, latest.downloadUrl)
            prefs.edit { putString("fw_notified", latest.version) }
        }
    }

    companion object {
        /** OK, needs calibration, warm-up, signal noise: no "sensor" notification. */
        private val OK_STATES = setOf(0x06, 0x07, 0x02, 0x03)
        private val END_STATES = setOf(0x01, 0x0f, 0x18, 0x1a, 0xc2)
        private const val STALE_S = 20 * 60

        /** The periodic check, plus one right away (each time the app is opened). */
        fun schedule(ctx: Context) {
            Alerts.createChannels(ctx)
            val wm = WorkManager.getInstance(ctx)
            wm.enqueueUniqueWork(
                "bridge-check-now",
                ExistingWorkPolicy.REPLACE,
                OneTimeWorkRequestBuilder<BridgeCheckWorker>().build(),
            )
            wm.enqueueUniquePeriodicWork(
                "bridge-check",
                ExistingPeriodicWorkPolicy.KEEP,
                PeriodicWorkRequestBuilder<BridgeCheckWorker>(15, TimeUnit.MINUTES).build(),
            )
        }
    }
}
