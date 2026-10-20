package com.bhubit.g7bridge.watch

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.bhubit.g7bridge.MainActivity
import com.bhubit.g7bridge.R

/**
 * The app's notifications. Glucose alarms stay in xDrip+; these only cover what xDrip+
 * cannot see: the bridge's battery, why no data arrives, certificates and updates.
 */
object Alerts {
    private const val CH_BATTERY = "battery"
    private const val CH_BATTERY_EMPTY = "battery_empty"
    // v2: without the app icon badge (a channel's badge setting cannot be changed later)
    private const val CH_DIAGNOSIS = "diagnosis_v2"
    private const val CH_INFO = "info_v2"

    const val ID_BATTERY = 1
    const val ID_DIAGNOSIS = 2
    const val ID_CERTS = 3
    const val ID_FIRMWARE = 4

    fun createChannels(ctx: Context) {
        val nm = ctx.getSystemService(NotificationManager::class.java)
        nm.deleteNotificationChannel("diagnosis")
        nm.deleteNotificationChannel("info")
        nm.createNotificationChannels(
            listOf(
                NotificationChannel(
                    CH_BATTERY, ctx.getString(R.string.ch_battery), NotificationManager.IMPORTANCE_DEFAULT,
                ),
                // Louder; the user may let this one through Do Not Disturb in the system settings
                NotificationChannel(
                    CH_BATTERY_EMPTY, ctx.getString(R.string.ch_battery_empty), NotificationManager.IMPORTANCE_HIGH,
                ),
                // Quiet and informational: no red badge on the app icon
                NotificationChannel(
                    CH_DIAGNOSIS, ctx.getString(R.string.ch_diagnosis), NotificationManager.IMPORTANCE_LOW,
                ).apply { setShowBadge(false) },
                NotificationChannel(CH_INFO, ctx.getString(R.string.ch_info), NotificationManager.IMPORTANCE_LOW)
                    .apply { setShowBadge(false) },
            ),
        )
    }

    fun canNotify(ctx: Context) =
        ContextCompat.checkSelfPermission(ctx, Manifest.permission.POST_NOTIFICATIONS) ==
            PackageManager.PERMISSION_GRANTED && NotificationManagerCompat.from(ctx).areNotificationsEnabled()

    fun batteryLow(ctx: Context, pct: Int, empty: Boolean) = show(
        ctx, ID_BATTERY, if (empty) CH_BATTERY_EMPTY else CH_BATTERY,
        ctx.getString(if (empty) R.string.n_bat_empty_title else R.string.n_bat_low_title, pct),
        ctx.getString(R.string.n_bat_text),
    )

    fun diagnosis(ctx: Context, title: String, text: String) =
        show(ctx, ID_DIAGNOSIS, CH_DIAGNOSIS, title, text)

    fun certsMissing(ctx: Context) = show(
        ctx, ID_CERTS, CH_INFO, ctx.getString(R.string.n_certs_title), ctx.getString(R.string.n_certs_text),
    )

    fun firmwareUpdate(ctx: Context, version: String) = show(
        ctx, ID_FIRMWARE, CH_INFO, ctx.getString(R.string.fw_new, version), ctx.getString(R.string.fw_update_text),
    )

    fun cancel(ctx: Context, id: Int) = NotificationManagerCompat.from(ctx).cancel(id)

    @android.annotation.SuppressLint("MissingPermission") // checked in canNotify()
    /** Intent extra: the history entry a tapped notification belongs to. */
    const val EXTRA_ENTRY = "notification_entry"

    private fun show(ctx: Context, id: Int, channel: String, title: String, text: String) {
        // Kept in the app's own history even when Android may not show it
        val entry = NotificationLog.add(ctx, title, text)
        if (!canNotify(ctx)) return
        val intent = Intent(ctx, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            .putExtra(EXTRA_ENTRY, entry)
        val pi = PendingIntent.getActivity(
            ctx, id, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val n = NotificationCompat.Builder(ctx, channel)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(title)
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setContentIntent(pi)
            .setAutoCancel(true)
            .setOnlyAlertOnce(true)
            .build()
        NotificationManagerCompat.from(ctx).notify(id, n)
    }
}
