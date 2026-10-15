package com.bhubit.g7bridge

import android.app.Activity
import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.os.Build
import android.os.LocaleList

/**
 * The app's language, switchable in the app (English / Polish); until then it follows the phone.
 * Android 13+ keeps the choice itself (also shown in the system's per-app language setting);
 * older versions get it from our preferences.
 *
 * The chosen language is also applied by hand, as the only one: Android adds the phone's
 * languages behind it, and with English living in the default resources it then picked Polish
 * after switching back to English (seen on Samsung, Android 15).
 */
object AppLanguage {
    private const val KEY = "language"

    fun set(activity: Activity, tag: String) {
        if (Build.VERSION.SDK_INT >= 33) {
            activity.getSystemService(LocaleManager::class.java).applicationLocales = LocaleList.forLanguageTags(tag)
        } else {
            activity.getSharedPreferences("bridge", Context.MODE_PRIVATE).edit().putString(KEY, tag).apply()
        }
        activity.recreate()
    }

    /** The chosen language for an activity or background work; the phone's if none was chosen. */
    fun wrap(base: Context): Context {
        val tag = chosen(base) ?: return base
        val config = Configuration(base.resources.configuration).apply { setLocales(LocaleList.forLanguageTags(tag)) }
        return base.createConfigurationContext(config)
    }

    private fun chosen(ctx: Context): String? =
        if (Build.VERSION.SDK_INT >= 33) {
            ctx.getSystemService(LocaleManager::class.java).applicationLocales
                .takeIf { !it.isEmpty }?.get(0)?.toLanguageTag()
        } else {
            ctx.getSharedPreferences("bridge", Context.MODE_PRIVATE).getString(KEY, null)
        }
}
