plugins { id("com.android.application") }
val inputs = providers.gradleProperty("beamInputs").get()
val flavor = providers.gradleProperty("beamFlavor").get()
val selectedComponents = providers.gradleProperty("beamComponents").orElse("file,network,zlib").get().split(",").filter { it.isNotEmpty() }
val keystorePath = providers.gradleProperty("beamKeystore").get()
val keystorePassword = providers.environmentVariable("BEAM_KEYSTORE_PASSWORD").get()
android {
    namespace = "org.erlangsters.acceptance.android"
    compileSdk = 36
    buildToolsVersion = "36.0.0"
    ndkVersion = "28.2.13676358"
    defaultConfig {
        applicationId = "org.erlangsters.acceptance.android"
        buildConfigField("boolean", "INVALID_NATIVE_SELECTION", providers.gradleProperty("beamNativeVariant").orElse("normal").get().let { (it == "invalid").toString() })
        minSdk = 26; targetSdk = 36; versionCode = 1; versionName = "1"
        ndk { abiFilters += setOf("arm64-v8a", "x86_64") }
        externalNativeBuild { cmake { arguments += listOf("-DBEAM_INPUTS=$inputs", "-DANDROID_STL=c++_shared", "-DFETCHCONTENT_FULLY_DISCONNECTED=ON", "-DBEAM_FIXTURE_COMPONENTS=${selectedComponents.joinToString(";")}") } }
    }
    signingConfigs {
        create("qualification") { storeFile = file(keystorePath); storePassword = keystorePassword; keyAlias = "qualification"; keyPassword = keystorePassword }
    }
    buildTypes {
        release { isDebuggable = false; isMinifyEnabled = false; signingConfig = signingConfigs.getByName("qualification") }
    }
    buildFeatures { prefab = true; buildConfig = true }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    sourceSets["main"].assets.srcDir("$inputs/assets")
    packaging { jniLibs { useLegacyPackaging = false } }
}
dependencies {
    implementation(files("$inputs/aars/beam-runtime-$flavor.aar"))
    selectedComponents.forEach { implementation(files("$inputs/aars/beam-$it-$flavor.aar")) }
}
dependencyLocking { lockAllConfigurations() }
