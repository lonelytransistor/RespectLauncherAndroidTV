plugins {
    id("com.android.application")
}

android {
    namespace = "net.lonelytransistor.respectlauncher"
    compileSdk = 34

    defaultConfig {
        applicationId = "net.lonelytransistor.respectlauncher"
        minSdk = 28
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        externalNativeBuild {
            cmake {
                cppFlags += ""
            }
        }
    }
    externalNativeBuild {
        cmake {
            path("native/CMakeLists.txt")
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    packagingOptions {
        jniLibs {
            useLegacyPackaging = true
        }
        exclude("META-INF/LICENSE.md")
        exclude("META-INF/LICENSE-notice.md")
    }
}

dependencies {
    implementation("androidx.leanback:leanback:1.0.0")
    implementation("androidx.appcompat:appcompat:1.6.1")
    implementation("com.google.android.material:material:1.11.0")
    implementation("androidx.constraintlayout:constraintlayout:2.1.4")
    implementation("dev.mobile:dadb:1.2.7")
    implementation(project(":commonlib"))
}