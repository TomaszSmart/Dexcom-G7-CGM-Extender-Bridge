package com.bhubit.g7bridge.ble

import org.json.JSONObject

/** The bridge's status JSON (characteristic a7c10002), see firmware/src/g7_ui.c build_status(). */
data class BridgeStatus(
    val code: String,
    val id: String,
    val searching: Boolean,
    val searchLeftS: Int,
    val phone: Boolean,
    /** xDrip+ really collects (has used the CGM service); null on firmware before 0.3. */
    val xdrip: Boolean?,
    val reading: Reading?,
    val session: Session,
    val seen: List<SeenSensor>,
    val phoneRssi: Int?,
    val battery: Battery?,
    /** "none", "stored", "builtin"; null on firmware without runtime certificates. */
    val certs: String?,
    val fw: String?,
    val msg: String,
) {
    data class Reading(val mgdl: Int, val state: Int, val ageS: Int, val trendTenths: Int?) {
        /** Only OK and "needs calibration" readings are passed on to xDrip+. */
        val usable get() = state == 0x06 || state == 0x07
    }

    data class Session(val minutes: Int?, val lengthMin: Int?, val startUtcS: Long?)

    data class SeenSensor(
        val name: String,
        val rssi: Int,
        val agoS: Int,
        val tried: Boolean,
        val use: Boolean,
    )

    data class Battery(val mv: Int, val pct: Int, val usb: Boolean)

    val inUse: SeenSensor? get() = seen.firstOrNull { it.use }

    companion object {
        fun parse(json: String): BridgeStatus {
            val o = JSONObject(json)
            val g = o.optJSONObject("g")
            val sess = o.optJSONObject("sess") ?: JSONObject()
            val seen = o.optJSONArray("seen")
            val bat = o.optJSONObject("bat")
            return BridgeStatus(
                code = o.optString("code"),
                id = o.optString("id"),
                searching = o.optInt("search") != 0,
                searchLeftS = o.optInt("left"),
                phone = o.optInt("phone") != 0,
                xdrip = if (o.has("xdrip")) o.getInt("xdrip") != 0 else null,
                reading = g?.let {
                    Reading(
                        mgdl = it.getInt("mgdl"),
                        state = it.getInt("state"),
                        ageS = it.optInt("age"),
                        trendTenths = if (it.has("trend")) it.getInt("trend") else null,
                    )
                },
                session = Session(
                    minutes = sess.optIntOrNull("min"),
                    lengthMin = sess.optIntOrNull("len"),
                    startUtcS = if (sess.has("start")) sess.getLong("start") else null,
                ),
                seen = (0 until (seen?.length() ?: 0)).map { i ->
                    val s = seen!!.getJSONObject(i)
                    SeenSensor(
                        name = s.optString("n"),
                        rssi = s.optInt("rssi"),
                        agoS = s.optInt("ago"),
                        tried = s.optInt("tried") != 0,
                        use = s.optInt("use") != 0,
                    )
                },
                phoneRssi = o.optIntOrNull("prssi"),
                battery = bat?.let { Battery(it.optInt("mv"), it.optInt("pct"), it.optInt("usb") != 0) },
                certs = if (o.has("certs")) o.getString("certs") else null,
                fw = if (o.has("fw")) o.getString("fw") else null,
                msg = o.optString("msg"),
            )
        }

        private fun JSONObject.optIntOrNull(key: String): Int? = if (has(key)) getInt(key) else null
    }
}
