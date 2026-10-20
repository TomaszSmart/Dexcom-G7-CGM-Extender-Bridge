plugins {
    id("com.android.application") version "8.13.2" apply false
    id("org.jetbrains.kotlin.android") version "2.2.21" apply false
    id("org.jetbrains.kotlin.plugin.compose") version "2.2.21" apply false
}

// Local builds can keep their output off OneDrive, which locks files in build/ mid-build:
// g7.buildRoot=C:/dev/g7android-build in ~/.gradle/gradle.properties. CI leaves it unset.
(findProperty("g7.buildRoot") as String?)?.let { root ->
    allprojects { layout.buildDirectory.set(file("$root/${project.name}")) }
}
