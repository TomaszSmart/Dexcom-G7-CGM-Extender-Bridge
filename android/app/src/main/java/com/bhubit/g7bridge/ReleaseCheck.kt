package com.bhubit.g7bridge

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import java.net.HttpURLConnection
import java.net.URL

/**
 * The latest firmware release on GitHub, to compare with the bridge's version. The same
 * repository also has app releases (tags app-v…): only firmware tags (v…) count here.
 */
object ReleaseCheck {
    const val REPO = "TomaszSmart/Dexcom-G7-CGM-Extender-Bridge"
    const val RELEASES = "https://github.com/$REPO/releases/latest"
    const val HOWTO = "https://github.com/$REPO/blob/main/firmware/README.md#install"

    data class Latest(val version: String, val downloadUrl: String)

    suspend fun latest(): Latest? = withContext(Dispatchers.IO) {
        try {
            val conn = URL("https://api.github.com/repos/$REPO/releases?per_page=30").openConnection() as HttpURLConnection
            conn.connectTimeout = 10_000
            conn.readTimeout = 10_000
            conn.setRequestProperty("Accept", "application/vnd.github+json")
            try {
                if (conn.responseCode != 200) return@withContext null
                val all = JSONArray(conn.inputStream.bufferedReader().readText())
                val j = (0 until all.length()).map { all.getJSONObject(it) }.firstOrNull {
                    !it.optBoolean("draft") && !it.optBoolean("prerelease") &&
                        Regex("""^v\d""").containsMatchIn(it.optString("tag_name"))
                } ?: return@withContext null
                val assets = j.optJSONArray("assets")
                val uf2 = (0 until (assets?.length() ?: 0)).map { assets!!.getJSONObject(it) }
                    .firstOrNull { Regex("rak4631.*\\.uf2$").containsMatchIn(it.optString("name")) }
                Latest(
                    version = j.getString("tag_name").removePrefix("v"),
                    downloadUrl = uf2?.optString("browser_download_url") ?: RELEASES,
                )
            } finally {
                conn.disconnect()
            }
        } catch (e: Exception) {
            null // offline: no update check
        }
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
