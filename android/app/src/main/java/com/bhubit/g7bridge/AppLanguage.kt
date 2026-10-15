package com.bhubit.g7bridge

import android.app.Activity
import android.app.LocaleManager
import android.content.Context
import android.content.res.Configuration
import android.os.Build
import android.os.LocaleList
import java.util.Locale

/**
 * The app's language, switchable in the app (English / Polish); until then it follows the phone.
 * Android 13+ keeps the choice itself (also shown in the system's per-app language setting);
 * older versions get it from our preferences.
 */
object AppLanguage {
    private const val KEY = "language"

    fun set(activity: Activity, tag: String) {
        if (Build.VERSION.SDK_INT >= 33) {
            activity.getSystemService(LocaleManager::class.java).applicationLocales = LocaleList.forLanguageTags(tag)
        } else {
            activity.getSharedPreferences("bridge", Context.MODE_PRIVATE).edit().putString(KEY, tag).apply()
            activity.recreate()
        }
    }

    /** Android 12 and older: the chosen language for an activity or background work. */
    fun wrap(base: Context): Context {
        if (Build.VERSION.SDK_INT >= 33) return base
        val tag = base.getSharedPreferences("bridge", Context.MODE_PRIVATE).getString(KEY, null) ?: return base
        val config = Configuration(base.resources.configuration).apply { setLocale(Locale.forLanguageTag(tag)) }
        return base.createConfigurationContext(config)
    }
}
