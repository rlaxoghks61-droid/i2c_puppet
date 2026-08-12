plugins {
    id("com.android.application")
}

android {
    namespace = "com.bbq20kbd.toolbar.test"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.bbq20kbd.toolbar.test"
        minSdk = 31
        targetSdk = 36
        versionCode = 1
        versionName = "0.1-test"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
