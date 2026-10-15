package com.bhubit.g7bridge.certs

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Base64
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.MultiFormatReader
import com.google.zxing.NotFoundException
import com.google.zxing.RGBLuminanceSource
import com.google.zxing.common.GlobalHistogramBinarizer
import com.google.zxing.common.HybridBinarizer
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.ByteArrayInputStream
import java.io.DataInputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.zip.GZIPInputStream

/**
 * The app certificates a new G7 asks for on its first pairing: xDrip+'s keks_p1 (certificate A),
 * keks_p2 (certificate B) and keks_p3 (the private key). They are never part of this app;
 * the user loads them from the QR code in xDrip+'s G7 documentation.
 */
object AppCerts {

    const val XDRIP_QR = "https://navid200.github.io/xDrip/docs/Dexcom/images/G7_keks_QR.png"

    class Unrecognised(val kind: Kind, detail: String? = null) : Exception(detail ?: kind.name) {
        enum class Kind { MISSING, TOO_SHORT, MALFORMED }
    }

    /** Certificate A, certificate B and the key, from an xdp2: QR text, a settings export or key=hex lines. */
    fun parse(input: String): List<ByteArray> {
        val text = input.trim()
        val parts: Map<String, ByteArray> = when {
            text.startsWith("xdp2:") -> decodeXdp2(text)
            text.startsWith("<") -> Regex("""<string\s+name="(keks_p[123])"\s*>([^<]*)</string>""")
                .findAll(text).associate { it.groupValues[1] to hexToBytes(it.groupValues[2]) }
            else -> Regex("""(keks_p[123])\s*[=:]\s*([0-9A-Fa-f\s:]+)""")
                .findAll(text).associate { it.groupValues[1] to hexToBytes(it.groupValues[2]) }
        }
        val a = parts["keks_p1"]
        val b = parts["keks_p2"]
        val k = parts["keks_p3"]
        if (a == null || b == null || k == null || k.isEmpty()) throw Unrecognised(Unrecognised.Kind.MISSING)
        if (a.size <= 100 || b.size <= 100) throw Unrecognised(Unrecognised.Kind.TOO_SHORT)
        return listOf(a, b, k)
    }

    /**
     * xDrip+ QRcodeUtils "xdp2:" format: base64 -> gzip -> u16 count, then
     * (u16 key length, key, u16 value length, value) big-endian; "b__" keys hold raw bytes.
     */
    private fun decodeXdp2(text: String): Map<String, ByteArray> {
        try {
            // Either base64 alphabet; Android's decoder does not need the padding
            val b64 = text.removePrefix("xdp2:").filterNot { it.isWhitespace() }
                .replace('-', '+').replace('_', '/')
            val raw = Base64.decode(b64, Base64.DEFAULT)
            val data = DataInputStream(GZIPInputStream(ByteArrayInputStream(raw)))
            val out = HashMap<String, ByteArray>()
            repeat(data.readUnsignedShort()) {
                val key = ByteArray(data.readUnsignedShort()).also { data.readFully(it) }
                val value = ByteArray(data.readUnsignedShort()).also { data.readFully(it) }
                out[key.decodeToString().removePrefix("b__")] = value
            }
            return out
        } catch (e: Exception) {
            throw Unrecognised(Unrecognised.Kind.MALFORMED, e.message)
        }
    }

    /** Like xDrip's tolerantHexStringToByteArray: ignores anything that is not a hex digit. */
    private fun hexToBytes(hex: String): ByteArray {
        val s = hex.filter { it.isDigit() || it.lowercaseChar() in 'a'..'f' }
        return ByteArray(s.length / 2) { i -> s.substring(2 * i, 2 * i + 2).toInt(16).toByte() }
    }

    // ---- QR codes -----------------------------------------------------------------

    /** The QR text in the documentation image, trying a few sizes and binarizers; null if none found. */
    fun readQr(bitmap: Bitmap): String? {
        val reader = MultiFormatReader().apply {
            setHints(
                mapOf(
                    DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE),
                    DecodeHintType.TRY_HARDER to true,
                ),
            )
        }
        val longest = maxOf(bitmap.width, bitmap.height)
        for (target in listOf(longest, 1600, 1000, 700)) {
            if (target > longest) continue
            val scaled = if (target == longest) bitmap else {
                val f = target.toFloat() / longest
                Bitmap.createScaledBitmap(bitmap, (bitmap.width * f).toInt(), (bitmap.height * f).toInt(), true)
            }
            val px = IntArray(scaled.width * scaled.height)
            scaled.getPixels(px, 0, scaled.width, 0, 0, scaled.width, scaled.height)
            val source = RGBLuminanceSource(scaled.width, scaled.height, px)
            for (bin in listOf(HybridBinarizer(source), GlobalHistogramBinarizer(source))) {
                try {
                    return reader.decodeWithState(BinaryBitmap(bin)).text
                } catch (_: NotFoundException) {
                } catch (_: Exception) {
                } finally {
                    reader.reset()
                }
            }
        }
        return null
    }

    /** The QR code from xDrip+'s G7 documentation (it is not hosted by this project). */
    suspend fun fetchXdripQr(): String = withContext(Dispatchers.IO) {
        val conn = URL(XDRIP_QR).openConnection() as HttpURLConnection
        try {
            conn.connectTimeout = 15_000
            conn.readTimeout = 15_000
            conn.useCaches = false
            if (conn.responseCode != 200) throw java.io.IOException("HTTP ${conn.responseCode}")
            val bmp = conn.inputStream.use { BitmapFactory.decodeStream(it) }
                ?: throw java.io.IOException("not an image")
            readQr(bmp) ?: throw Unrecognised(Unrecognised.Kind.MISSING, "no QR code")
        } finally {
            conn.disconnect()
        }
    }
}
