# Repository Guidelines

## Project Structure & Module Organization
This repository is an Android app with a native C++ game engine. The main app lives in `app/`, Android sources are under `app/src/main/`, the Android JNI wrapper lives under `app/src/main/cpp/android/`, and shared native logic lives in `native/`. Key native subfolders are `core/`, `humanplay/`, `benchmark/`, and `tools/`. Shared CLI configs live in `configs/`, a root `scripts/` directory is reserved for future automation, and UI assets live in `app/src/main/res/`.

## Build, Test, and Development Commands
- `./gradlew assembleDebug`: build the Android app.
- `./gradlew test`: run JVM unit tests if present.
- `./gradlew connectedAndroidTest`: run instrumentation tests on a device/emulator; use only when Android changes need validation.
- `cmake -S native -B native/build -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DCMAKE_BUILD_TYPE=Release` then `cmake --build native/build -j`: build the native C++ core from `native/`.
- For Python tooling in `native/tools/`, create a separate conda env named `gameai`, install `requirements.txt`, then run `python decide_params.py` from that directory.

## Coding Style & Naming Conventions
Use the existing language defaults: 4-space indentation for Kotlin, Gradle Kotlin, and C++; keep brace style consistent with nearby files. Kotlin/Java types use `UpperCamelCase`, functions and variables use `lowerCamelCase`, and Android resources use `snake_case` (for example `root_preferences.xml`). Keep CMake targets and native filenames aligned with their folder purpose.

## Testing Guidelines
Prefer validating the native engine and Python tooling first. Run native builds from `native/` and use the `native/tools/` scripts in the `gameai` conda environment when checking parameter search or self-play helpers. Avoid Android emulator or device testing unless the change touches UI, JNI, or packaging. Name new tests to match the feature or module they cover.

## Commit & Pull Request Guidelines
The visible history does not show a strong commit-message convention, so use short imperative subjects such as `fix native bridge` or `add benchmark config`. PRs should describe the change, list the commands used to verify it, and include screenshots for UI updates. Note any Android validation gaps explicitly if you only tested the native or Python side.

## Agent-Specific Instructions
Do not modify generated build outputs or local environment files. When testing is needed, prefer the native and Python paths first and keep Android checks as a last resort.
