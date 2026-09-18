import java.util.Properties

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

val uploadSigning = Properties()
val uploadSigningFile = rootProject.file("key.properties")
if (uploadSigningFile.isFile) {
    uploadSigningFile.inputStream().use { uploadSigning.load(it) }
}
if (gradle.startParameter.taskNames.any { it.contains("release", ignoreCase = true) }) {
    listOf("storeFile", "storePassword", "keyAlias", "keyPassword").forEach { property ->
        require(!uploadSigning.getProperty(property).isNullOrBlank()) {
            "Release signing is missing $property. Configure android/key.properties; see README.md."
        }
    }
    require(rootProject.file(uploadSigning.getProperty("storeFile")).isFile) {
        "The upload keystore is missing. Restore it from your secure backup."
    }
}

android {
    namespace = "at.innocharge.innocharge_mobile"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        applicationId = "at.innocharge.innocharge_mobile"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        versionCode = flutter.versionCode
        versionName = flutter.versionName
    }

    signingConfigs {
        create("release") {
            keyAlias = uploadSigning.getProperty("keyAlias")
            keyPassword = uploadSigning.getProperty("keyPassword")
            storeFile = uploadSigning.getProperty("storeFile")?.let { rootProject.file(it) }
            storePassword = uploadSigning.getProperty("storePassword")
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.getByName("release")
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}
