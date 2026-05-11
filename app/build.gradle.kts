plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.android)
}

android {
    namespace = "com.aiprojects.gameai"
    compileSdk = 36

    // 启用按 ABI 拆包
    splits {
        abi {
            isEnable = true                    // 打开 split
            reset()                            // 清空默认列表
            include("arm64-v8a", "x86_64")     // 只打这两个
            isUniversalApk = false             // 不要整包
        }
    }

    defaultConfig {
        applicationId = "com.aiprojects.gameai"
        minSdk = 28
        targetSdk = 36
        versionCode = 2
        versionName = "1.1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        externalNativeBuild {
            cmake {
                abiFilters("arm64-v8a", "x86_64")
                arguments("-DANDROID_STL=c++_shared",
                        "-DANDROID_BUILD=ON",
                        "-DENABLE_MCTS_PURE=ON",
                        "-DCMAKE_BUILD_TYPE=Release")
            }
        }
    }

    // 把 versionName 注入到字符串资源
    // 根据 split-abi 分别重命名
    applicationVariants.all {
        resValue("string", "app_version_name", "\"${versionName}\"")
        val variant = this
        variant.outputs.all {
            val abi = filters.find { it.identifier in setOf("arm64-v8a", "x86_64") }?.identifier
            if (abi != null) {
                val fileName = "GameAI-${abi}-${versionName}-${variant.buildType.name}.apk"
                (this as com.android.build.gradle.internal.api.BaseVariantOutputImpl)
                    .outputFileName = fileName
            }
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
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    kotlinOptions {
        jvmTarget = "11"
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
    buildFeatures {
        viewBinding = true
    }
}

dependencies {
    implementation("com.google.android.material:material:1.11.0")
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.androidx.preference)
    implementation(libs.material)
    implementation(libs.androidx.constraintlayout)
    implementation(libs.androidx.preference)
    implementation(libs.androidx.activity)
    implementation(libs.androidx.recyclerview)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}
