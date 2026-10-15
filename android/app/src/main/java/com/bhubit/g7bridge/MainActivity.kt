package com.bhubit.g7bridge

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import com.bhubit.g7bridge.ui.BridgeScreen
import com.bhubit.g7bridge.ui.G7Theme
import com.bhubit.g7bridge.watch.BridgeWatch

class MainActivity : ComponentActivity() {

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
        setContent {
            G7Theme {
                BridgeScreen(vm)
            }
        }
    }
}
