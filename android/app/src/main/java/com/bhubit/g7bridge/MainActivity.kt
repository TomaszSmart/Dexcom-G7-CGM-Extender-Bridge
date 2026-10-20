package com.bhubit.g7bridge

import android.content.Context
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import com.bhubit.g7bridge.ui.BridgeScreen
import com.bhubit.g7bridge.ui.G7Theme
import android.content.Intent
import com.bhubit.g7bridge.watch.Alerts
import com.bhubit.g7bridge.watch.BridgeWatch

class MainActivity : ComponentActivity() {

    override fun attachBaseContext(newBase: Context) = super.attachBaseContext(AppLanguage.wrap(newBase))

    private val vm: MainViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        BridgeWatch.schedule(this)
        // Read the status only while the app is on screen
        lifecycle.addObserver(object : DefaultLifecycleObserver {
            override fun onStart(owner: LifecycleOwner) = vm.setForeground(true)
            override fun onStop(owner: LifecycleOwner) = vm.setForeground(false)
        })
        handleIntent(intent)
        setContent {
            G7Theme {
                BridgeScreen(vm)
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        val entry = intent?.getLongExtra(Alerts.EXTRA_ENTRY, -1L) ?: -1L
        if (entry >= 0) {
            vm.openNotifications(entry)
            intent?.removeExtra(Alerts.EXTRA_ENTRY) // not again after a rotation
        }
    }
}
