# Repository Guidelines

## Project Structure & Module Organization
This repository is an Android app with a native C++ game engine. The main app lives in `app/`, Android sources are under `app/src/main/`, and native logic is under `app/src/main/cpp/UTT/`. Key native subfolders are `core/`, `humanplay/`, `benchmark/`, `interface/`, and `tool/`. UI assets live in `app/src/main/res/`, and screenshots/docs are in `img/`.

## Build, Test, and Development Commands
- `./gradlew assembleDebug`: build the Android app.
- `./gradlew test`: run JVM unit tests if present.
- `./gradlew connectedAndroidTest`: run instrumentation tests on a device/emulator; use only when Android changes need validation.
- `cmake -B build -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DCMAKE_BUILD_TYPE=Release` then `cmake --build build -j`: build the native C++ core from `app/src/main/cpp/UTT/`.
- For Python tooling in `app/src/main/cpp/UTT/tool/`, create a separate conda env named `gameai`, install `requirements.txt`, then run `python decide_params.py`.

## Coding Style & Naming Conventions
Use the existing language defaults: 4-space indentation for Kotlin, Gradle Kotlin, and C++; keep brace style consistent with nearby files. Kotlin/Java types use `UpperCamelCase`, functions and variables use `lowerCamelCase`, and Android resources use `snake_case` (for example `root_preferences.xml`). Keep CMake targets and native filenames aligned with their folder purpose.

## Testing Guidelines
Prefer validating the native engine and Python tooling first. Run native builds in `app/src/main/cpp/UTT/` and use the `tool/` scripts in the `gameai` conda environment when checking parameter search or self-play helpers. Avoid Android emulator or device testing unless the change touches UI, JNI, or packaging. Name new tests to match the feature or module they cover.

## Commit & Pull Request Guidelines
The visible history does not show a strong commit-message convention, so use short imperative subjects such as `fix native bridge` or `add benchmark config`. PRs should describe the change, list the commands used to verify it, and include screenshots for UI updates. Note any Android validation gaps explicitly if you only tested the native or Python side.

## Agent-Specific Instructions
Do not modify generated build outputs or local environment files. When testing is needed, prefer the native and Python paths first and keep Android checks as a last resort.
