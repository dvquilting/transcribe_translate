// Transcribe & Translate for Android.
//
// The native engine (libpkengine.so) is CrispASR + its built-in llama.cpp + the
// desktop programs' C++ sources (../transcribe_cpp, ../translate_cpp, ../common),
// configured through CrispASR's own CMakeLists.txt with our overlay
// (src/main/cpp/android_overlay.cmake), the same way the desktop static build works.
//
// Gradle properties:
//   -PcrispasrDir=PATH   CrispASR checkout (default ../transcribe_cpp/third_party/CrispASR,
//                        which build_apk.sh/.bat fetch at the pinned commit)
//   -PcompatibleCpu=true build for any 64-bit phone from ~2019 on (Armv8.2 + dot product)
//                        instead of the default, which also needs the int8 matrix
//                        instructions (i8mm) of Snapdragon 8 Gen 1 and newer - faster

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val crispasrDir: String = (project.findProperty("crispasrDir") as String?)
    ?: rootProject.file("../transcribe_cpp/third_party/CrispASR").absolutePath
val compatibleCpu = (project.findProperty("compatibleCpu") as String?) == "true"
val armArch = if (compatibleCpu) "armv8.2-a+dotprod+fp16" else "armv8.2-a+dotprod+fp16+i8mm"

android {
    namespace = "com.davidv.transcribe"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.davidv.transcribe"
        minSdk = 29
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
        ndk { abiFilters += listOf("arm64-v8a") }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCMAKE_PROJECT_crispasr_INCLUDE=${file("src/main/cpp/android_overlay.cmake").absolutePath}",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                    "-DBUILD_SHARED_LIBS=OFF",
                    "-DCRISPASR_BUILD_TESTS=OFF",
                    "-DCRISPASR_BUILD_EXAMPLES=OFF",
                    "-DCRISPASR_BUILD_SERVER=OFF",
                    "-DGGML_NATIVE=OFF",
                    "-DGGML_OPENMP=OFF",          // ggml's own thread pool: no libomp.so to ship
                    "-DGGML_LLAMAFILE=ON",
                    "-DGGML_CPU_ARM_ARCH=$armArch",
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",  // 16 KB pages (Android 15+)
                )
                cppFlags += listOf("-O3")
                targets += listOf("pkengine")
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("$crispasrDir/CMakeLists.txt")
            version = "3.31.6"
        }
    }

    buildTypes {
        debug {
            // The engine is always built optimized: a debug build would be 10-50x slower.
            externalNativeBuild { cmake { arguments += "-DCMAKE_BUILD_TYPE=Release" } }
        }
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            // Signed with the debug key so the APK installs as is (sideloading); see README.md
            // for a key of your own.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
    buildFeatures { compose = true }
    packaging { jniLibs { useLegacyPackaging = false } }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2025.06.01")
    implementation(composeBom)
    implementation("androidx.core:core-ktx:1.16.0")
    implementation("androidx.activity:activity-compose:1.10.1")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.9.1")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.9.1")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.9.1")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.10.2")
    debugImplementation("androidx.compose.ui:ui-tooling")
    implementation("androidx.compose.ui:ui-tooling-preview")
    testImplementation("junit:junit:4.13.2")
}
