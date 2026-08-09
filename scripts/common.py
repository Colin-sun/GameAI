#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

import json5


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def load_native_module():
    # Prefer the repository build so scripts exercise the current native
    # sources instead of an older globally installed extension.
    module_dir = repo_root() / "build" / "native" / "python"
    if module_dir.exists():
        sys.path.insert(0, str(module_dir))
        try:
            import gameai_native  # pylint: disable=import-error

            return gameai_native
        except ModuleNotFoundError:
            sys.path.pop(0)

    try:
        import gameai_native  # pylint: disable=import-error

        return gameai_native
    except ModuleNotFoundError:
        pass

    raise ModuleNotFoundError(
        "gameai_native is unavailable. Run `python -m pip install .` "
        "or build the Python bindings with CMake."
    )


def load_json5_config(config_path: Path) -> dict:
    with config_path.open("r", encoding="utf-8") as handle:
        return json5.load(handle)
