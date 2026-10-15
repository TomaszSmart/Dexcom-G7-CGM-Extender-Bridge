package com.bhubit.g7bridge.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

/** Glucose and status colours, the same meaning as on the setup page. */
object Tone {
    val Good = Color(0xFF2E9E5B)
    val Warn = Color(0xFFE0A100)
    val Bad = Color(0xFFD64545)
}

@Composable
fun G7Theme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val ctx = LocalContext.current
    val scheme = when {
        Build.VERSION.SDK_INT >= 31 -> if (dark) dynamicDarkColorScheme(ctx) else dynamicLightColorScheme(ctx)
        dark -> darkColorScheme(primary = Color(0xFF6FD3A8))
        else -> lightColorScheme(primary = Color(0xFF0B6E4F))
    }
    MaterialTheme(colorScheme = scheme, content = content)
}
