# Repository Guidelines

## Project Structure & Module Organization
This repository is an Android app with a native C++ game engine. The main app lives in `app/`, Android sources are under `app/src/main/`, the Android JNI wrapper lives under `app/src/main/cpp/android/`, and shared native logic lives in `native/`. Key native subfolders are `core/` and `python/`. Shared CLI configs live in `configs/`, Python entry scripts live in `scripts/`, and UI assets live in `app/src/main/res/`.

## Build, Test, and Development Commands
- By default, only run python & cpp test and do not modify Kotlin and java code
- `python -m pip install .`: install Python dependencies and build the `gameai_native` binding for Python tools.
- `cmake -S native -B build/native -DLINUX_BUILD=ON -DENABLE_MCTS_PURE=ON -DENABLE_PYTHON_BINDINGS=ON -DCMAKE_BUILD_TYPE=Release` then `cmake --build build/native -j`: build the native C++ core plus Python bindings from `native/`, with all artifacts kept under the repo-root `build/`.
- `python scripts/decide_params.py --trials 10 --games-per-side 2`: run Optuna-based MCTS tuning through the Python bindings.
- `python scripts/benchmark.py --config configs/benchmark/benchmark_config.json5`: run the empty-board benchmark through the Python bindings.
- `python scripts/humanplay.py --config configs/humanplay/humanplay_config.json5`: run the terminal human-play interface backed by the Python bindings.
- For Python tooling, create a separate conda env named `gameai`, install root `pyproject.toml` with `python -m pip install .`, then run the scripts from the repo root.

## Coding Style & Naming Conventions
Use the existing language defaults: 4-space indentation for Kotlin, Gradle Kotlin, and C++; keep brace style consistent with nearby files. Kotlin/Java types use `UpperCamelCase`, functions and variables use `lowerCamelCase`, and Android resources use `snake_case` (for example `root_preferences.xml`). Keep CMake targets and native filenames aligned with their folder purpose.

## Testing Guidelines
Prefer validating the native engine and Python tooling first. Run native builds from `native/` only when checking the binding itself, and use the `scripts/` entry points in the `gameai` conda environment for benchmark, tuning, or terminal gameplay. Avoid Android emulator or device testing unless the change touches UI, JNI, or packaging. Name new tests to match the feature or module they cover.

## Commit & Pull Request Guidelines
The visible history does not show a strong commit-message convention, so use short imperative subjects such as `fix native bridge` or `add benchmark config`. PRs should describe the change, list the commands used to verify it, and include screenshots for UI updates. Note any Android validation gaps explicitly if you only tested the native or Python side.

## Agent-Specific Instructions
Do not modify generated build outputs or local environment files. When testing is needed, prefer the native and Python paths first and keep Android checks as a last resort.

# Game Rule Constraint
The default Ultimate Tic-Tac-Toe rule is `majority-utt-v1`:

- A local 3x3 board is won only by its ordinary row, column, or diagonal three-in-a-row.
- The meta board is decided by the number of local boards won by each player.
- Meta-board rows, columns, and diagonals have no winning meaning.
- A player wins as soon as they have an irreversible strict majority of local boards.
- Drawn local boards do not count toward either player's majority.
- If all local boards are resolved without a strict majority, the result is the larger count, or a draw when counts are equal.

All heuristics, training targets, search code, and evaluations must use this rule. Do not reintroduce meta three-in-a-row as a terminal condition or heuristic reward.
