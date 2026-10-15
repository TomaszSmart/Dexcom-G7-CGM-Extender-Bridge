package com.bhubit.g7bridge

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * The latest releases on GitHub, to compare with the bridge's firmware and with this app. The
 * repository has firmware releases (tags v…, a .uf2) and app releases (tags app-v…, an .apk).
 */
object ReleaseCheck {
    const val REPO = "TomaszSmart/Dexcom-G7-CGM-Extender-Bridge"
    const val RELEASES = "https://github.com/$REPO/releases"
    const val HOWTO = "https://github.com/$REPO/blob/main/firmware/README.md#install"
    const val APP_HOWTO = "https://github.com/$REPO/blob/main/android/README.md#install"
    const val APP_HOWTO_PL = "https://github.com/$REPO/blob/main/android/README.pl.md#instalacja"

    data class Latest(val version: String, val downloadUrl: String)
    data class Releases(val firmware: Latest?, val app: Latest?)

    suspend fun releases(): Releases? = withContext(Dispatchers.IO) {
        try {
            val conn = URL("https://api.github.com/repos/$REPO/releases?per_page=30").openConnection() as HttpURLConnection
            conn.connectTimeout = 10_000
            conn.readTimeout = 10_000
            conn.setRequestProperty("Accept", "application/vnd.github+json")
            try {
                if (conn.responseCode != 200) return@withContext null
                val all = JSONArray(conn.inputStream.bufferedReader().readText())
                val list = (0 until all.length()).map { all.getJSONObject(it) }
                    .filter { !it.optBoolean("draft") && !it.optBoolean("prerelease") }
                Releases(
                    firmware = newest(list, "v", Regex("rak4631.*\\.uf2$")),
                    app = newest(list, "app-v", Regex("\\.apk$")),
                )
            } finally {
                conn.disconnect()
            }
        } catch (e: Exception) {
            null // offline: no update check
        }
    }

    /** The bridge firmware only (the background check). */
    suspend fun latest(): Latest? = releases()?.firmware

    private fun newest(list: List<JSONObject>, prefix: String, asset: Regex): Latest? {
        val j = list.firstOrNull { Regex("^" + Regex.escape(prefix) + "\\d").containsMatchIn(it.optString("tag_name")) }
            ?: return null
        val assets = j.optJSONArray("assets")
        val file = (0 until (assets?.length() ?: 0)).map { assets!!.getJSONObject(it) }
            .firstOrNull { asset.containsMatchIn(it.optString("name")) }
        return Latest(
            version = j.getString("tag_name").removePrefix(prefix),
            downloadUrl = file?.optString("browser_download_url") ?: RELEASES,
        )
    }

    private fun parse(v: String?): List<Int>? =
        v?.takeIf { Regex("""^\d+(\.\d+)*$""").matches(it) }?.split('.')?.map { it.toInt() }

    enum class Verdict { UNKNOWN, CURRENT, DEV_BUILD, UPDATE }

    fun compare(installed: String?, latest: Latest?): Verdict {
        val cur = parse(installed)
        val lat = parse(latest?.version)
        return when {
            installed == null -> Verdict.UNKNOWN
            cur == null -> Verdict.DEV_BUILD // e.g. "0.2-dev": not comparable with a release
            lat != null && newer(lat, cur) -> Verdict.UPDATE
            lat != null -> Verdict.CURRENT
            else -> Verdict.UNKNOWN
        }
    }

    private fun newer(a: List<Int>, b: List<Int>): Boolean {
        for (i in 0 until maxOf(a.size, b.size)) {
            val x = a.getOrElse(i) { 0 }
            val y = b.getOrElse(i) { 0 }
            if (x != y) return x > y
        }
        return false
    }
}
