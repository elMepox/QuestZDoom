plugins {
    id("com.android.application")
}

val uzdoomDir = rootProject.layout.projectDirectory.dir("external/UZDoom")
val sdlDir = rootProject.layout.projectDirectory.dir("external/SDL")
// Engine data (uzdoom.pk3, soundfonts, ...) produced by the native build, packaged as APK assets.
val generatedAssets = layout.buildDirectory.dir("generated/uzdoom-assets")
val openalDir = rootProject.layout.projectDirectory.dir("external/openal-soft")
// Freedoom IWADs fetched by scripts/fetch-freedoom.sh (checksum-verified).
val freedoomAssets = layout.buildDirectory.dir("generated/freedoom-assets")

android {
    namespace = "io.github.elmepox.doomxr"
    compileSdk = 36
    // r28+ links with 16 KB page alignment by default.
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "io.github.elmepox.doomxr"
        minSdk = 32
        targetSdk = 34
        versionCode = 1
        versionName = "0.1.0"

        ndk {
            abiFilters += "arm64-v8a"
        }

        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                    "-DDOOMXR_SDL_DIR=${sdlDir.asFile.absolutePath}",
                    "-DDOOMXR_OPENAL_DIR=${openalDir.asFile.absolutePath}",
                    "-DDOOMXR_ASSET_DIR=${generatedAssets.get().asFile.absolutePath}",
                    "-DDOOMXR_HOST_TOOLS_SRC=${file("src/main/cpp/host-tools").absolutePath}",
                    "-DCMAKE_PROJECT_UZDoom_INCLUDE=${file("src/main/cpp/uzdoom_hook.cmake").absolutePath}",
                    // UZDoom options
                    "-DZDOOM_EXE_NAME:STRING=main",
                    "-DHAVE_VULKAN=OFF",
                    "-DHAVE_GLES2=ON",
                    "-DNO_OPENAL=OFF",
                    "-DDYN_OPENAL=ON",
                    "-DFORCE_NO_LTO=ON",
                    "-DBUILD_NONFREE=OFF",
                    "-DDYN_SNDFILE=ON",
                    "-DDYN_MPG123=ON",
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            // The engine is built straight from its own CMake project; our hook file
            // (uzdoom_hook.cmake) adds SDL2 and the host tools to it.
            path = uzdoomDir.file("CMakeLists.txt").asFile
            version = "3.31.6"
        }
    }

    buildTypes {
        debug {
            isDebuggable = true
        }
        release {
            isMinifyEnabled = false
            // Debug-signed for now so release builds can be sideloaded; replace with a real key for distribution.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            // Keep native libs uncompressed & page-aligned in the APK (mmap'd directly).
            useLegacyPackaging = false
        }
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
}

androidComponents {
    onVariants { variant ->
        // SDL's Java glue (SDLActivity & friends) comes straight from the submodule.
        variant.sources.java?.addStaticSourceDirectory(
            sdlDir.dir("android-project/app/src/main/java").asFile.path)
        // Engine data written by the native build (see DOOMXR_ASSET_DIR).
        variant.sources.assets?.addStaticSourceDirectory(generatedAssets.get().asFile.path)
        variant.sources.assets?.addStaticSourceDirectory(freedoomAssets.get().asFile.path)
    }
}

// --- engine patches -------------------------------------------------------------------------
// UZDoom is used unmodified as a submodule; the few Android tweaks live in patches/uzdoom.
val applyUzdoomPatches = tasks.register<Exec>("applyUzdoomPatches") {
    group = "doomxr"
    description = "Applies patches/uzdoom/*.patch to the UZDoom submodule (idempotent)."
    workingDir = rootProject.projectDir
    commandLine("bash", "scripts/apply-patches.sh")
}

val fetchFreedoom = tasks.register<Exec>("fetchFreedoom") {
    group = "doomxr"
    description = "Downloads Freedoom into the APK assets (skipped when already present)."
    workingDir = rootProject.projectDir
    val dest = freedoomAssets.get().asFile.resolve("freedoom")
    commandLine("bash", "scripts/fetch-freedoom.sh", dest.absolutePath)
}

tasks.configureEach {
    if (name.startsWith("configureCMake") || name.startsWith("generateJsonModel")) {
        dependsOn(applyUzdoomPatches)
    }
}

// Assets are produced by the native build (see DOOMXR_ASSET_DIR), so merging must wait for it.
afterEvaluate {
    listOf("Debug", "Release").forEach { variant ->
        tasks.matching { it.name == "merge${variant}Assets" }.configureEach {
            dependsOn(fetchFreedoom)
            dependsOn("buildCMake${if (variant == "Debug") "Debug" else "RelWithDebInfo"}[arm64-v8a]")
        }
    }
}
