import java.util.Properties

plugins {
    alias(libs.plugins.android.application)
}

// One version for every platform: read it from the CMake project.
val synaxisVersion: String = Regex("""project\(Synaxis VERSION (\d+)\.(\d+)\.(\d+)""")
    .find(rootProject.file("../CMakeLists.txt").readText())
    ?.groupValues?.drop(1)?.joinToString(".")
    ?: error("No version in ../CMakeLists.txt")
val synaxisVersionCode: Int = synaxisVersion.split(".").map(String::toInt)
    .let { (major, minor, patch) -> major * 10000 + minor * 100 + patch }

// Release signing: keystore.properties (local builds) or SYNAXIS_* environment
// variables (CI). Without either, release builds come out unsigned.
val signing = Properties().apply {
    val file = rootProject.file("keystore.properties")
    if (file.exists()) file.inputStream().use(::load)
    System.getenv("SYNAXIS_KEYSTORE")?.let { setProperty("storeFile", it) }
    System.getenv("SYNAXIS_KEYSTORE_PASSWORD")?.let { setProperty("storePassword", it) }
    System.getenv("SYNAXIS_KEY_ALIAS")?.let { setProperty("keyAlias", it) }
    System.getenv("SYNAXIS_KEY_PASSWORD")?.let { setProperty("keyPassword", it) }
}

android {
    namespace = "io.synaxis.app"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "io.synaxis.app"
        minSdk = 29
        targetSdk = 37
        versionCode = synaxisVersionCode
        versionName = synaxisVersion
    }

    signingConfigs {
        create("release") {
            if (signing.containsKey("storeFile")) {
                storeFile = file(signing.getProperty("storeFile"))
                storePassword = signing.getProperty("storePassword")
                keyAlias = signing.getProperty("keyAlias")
                keyPassword = signing.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            if (signing.containsKey("storeFile")) {
                signingConfig = signingConfigs.getByName("release")
            }
            optimization {
                enable = true
            }
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // The same web UI as the desktop app, served from the APK's assets.
    sourceSets {
        getByName("main") {
            assets.directories.add(rootProject.file("../web").path)
        }
    }

    testOptions {
        unitTests.all {
            it.systemProperty("synaxis.fixtures", rootProject.file("../tests/fixtures").path)
        }
    }
}

dependencies {
    implementation(libs.androidx.activity.ktx)
    implementation(libs.androidx.core.ktx)

    testImplementation(libs.junit)
    testImplementation(libs.org.json)  // the real implementation; android.jar only has stubs
}
