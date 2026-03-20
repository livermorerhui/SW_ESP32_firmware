plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.sonicwave.transport"
    compileSdk = 36

    defaultConfig {
        minSdk = 26
        consumerProguardFiles("consumer-rules.pro")
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    api(project(":sonicwave-protocol"))
    implementation(libs.androidx.core.ktx)
    implementation(libs.kotlinx.coroutines.android)
}
