plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

val repo: File = rootDir.parentFile
val stage = file(providers.gradleProperty("openbgi.stage").getOrElse("$repo/build/android-arm64/apk"))
val sdlJava = file(
    providers.gradleProperty("openbgi.sdlJava")
        .getOrElse("$repo/build/android-arm64/_deps/sdl3-src/android-project/app/src/main/java")
)

val engineVersion: String = Regex("""project\(OpenBGI VERSION ([0-9.]+)""")
    .find(File(repo, "CMakeLists.txt").readText())?.groupValues?.get(1) ?: "0.1.0"
val engineVersionCode: Int = engineVersion.split('.').map { it.toInt() }.let { v ->
    v[0] * 10000 + v.getOrElse(1) { 0 } * 100 + v.getOrElse(2) { 0 }
}

android {
    namespace = "org.openbgi"
    compileSdk = 36

    defaultConfig {
        applicationId = "org.openbgi.launcher"
        minSdk = 26
        targetSdk = 35
        versionCode = engineVersionCode
        versionName = engineVersion
        ndk { abiFilters += "arm64-v8a" }
    }

    sourceSets {
        getByName("main") {
            manifest.srcFile("AndroidManifest.xml")
            java.srcDirs("java", "../java", sdlJava)
            kotlin.srcDirs("kotlin")
            res.srcDirs("res")
            assets.srcDirs(File(stage, "assets"))
            jniLibs.srcDirs(File(stage, "lib"))
        }
    }

    buildFeatures { compose = true }

    packaging { jniLibs { useLegacyPackaging = false } }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            signingConfig = null
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    lint { checkReleaseBuilds = false }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.navigation.compose)
    debugImplementation(libs.androidx.compose.ui.tooling)
}
