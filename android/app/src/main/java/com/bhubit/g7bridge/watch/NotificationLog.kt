package com.bhubit.g7bridge.watch

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * The app's own history of its notifications (the "Notifications" screen): kept after the system
 * notification is gone, newest first, last [MAX] entries, in a small JSON file.
 */
object NotificationLog {
    private const val MAX = 100
    private const val FILE = "notifications.json"

    data class Entry(val id: Long, val timeMs: Long, val title: String, val text: String, val read: Boolean)

    private val _entries = MutableStateFlow<List<Entry>>(emptyList())
    val entries: StateFlow<List<Entry>> = _entries
    @Volatile private var loaded = false

    fun load(ctx: Context): StateFlow<List<Entry>> {
        if (!loaded) {
            synchronized(this) {
                if (!loaded) {
                    _entries.value = read(ctx)
                    loaded = true
                }
            }
        }
        return entries
    }

    /** Returns the new entry's id (passed along by the notification's tap). */
    fun add(ctx: Context, title: String, text: String): Long = synchronized(this) {
        load(ctx)
        val now = System.currentTimeMillis()
        val id = maxOf(now, (_entries.value.firstOrNull()?.id ?: 0) + 1)
        update(ctx, (listOf(Entry(id, now, title, text, read = false)) + _entries.value).take(MAX))
        id
    }

    fun markRead(ctx: Context, id: Long) = synchronized(this) {
        load(ctx)
        if (_entries.value.any { it.id == id && !it.read }) {
            update(ctx, _entries.value.map { if (it.id == id) it.copy(read = true) else it })
        }
    }

    fun markAllRead(ctx: Context) = synchronized(this) {
        load(ctx)
        update(ctx, _entries.value.map { it.copy(read = true) })
    }

    fun clear(ctx: Context) = synchronized(this) { update(ctx, emptyList()) }

    private fun update(ctx: Context, list: List<Entry>) {
        _entries.value = list
        val json = JSONArray()
        list.forEach {
            json.put(
                JSONObject().put("id", it.id).put("t", it.timeMs).put("title", it.title)
                    .put("text", it.text).put("read", it.read),
            )
        }
        File(ctx.filesDir, FILE).writeText(json.toString())
    }

    private fun read(ctx: Context): List<Entry> = try {
        val json = JSONArray(File(ctx.filesDir, FILE).readText())
        (0 until json.length()).map { i ->
            json.getJSONObject(i).let {
                Entry(it.getLong("id"), it.getLong("t"), it.getString("title"), it.getString("text"), it.getBoolean("read"))
            }
        }
    } catch (e: Exception) {
        emptyList() // no history yet
    }
}
