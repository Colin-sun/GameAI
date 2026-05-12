from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


ROOT_DIR = Path(__file__).resolve().parent
NATIVE_DIR = ROOT_DIR / "native"
ROOT_BUILD_DIR = ROOT_DIR / "build"


class CMakeExtension(Extension):
    def __init__(self, name: str) -> None:
        super().__init__(name, sources=[])


class CMakeBuild(build_ext):
    def build_extension(self, ext: Extension) -> None:
        ext_fullpath = Path(self.get_ext_fullpath(ext.name)).resolve()
        extdir = ext_fullpath.parent
        build_dir = (ROOT_BUILD_DIR / "python-install" / ext.name).resolve()
        build_dir.mkdir(parents=True, exist_ok=True)
        extdir.mkdir(parents=True, exist_ok=True)

        config = "Debug" if self.debug else "Release"

        import pybind11

        cmake_args = [
            f"-DCMAKE_BUILD_TYPE={config}",
            "-DLINUX_BUILD=ON",
            "-DENABLE_MCTS_PURE=ON",
            "-DENABLE_PYTHON_BINDINGS=ON",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-Dpybind11_DIR={pybind11.get_cmake_dir()}",
        ]
        build_args = ["--config", config, "-j"]

        subprocess.check_call(
            ["cmake", "-S", str(NATIVE_DIR), "-B", str(build_dir), *cmake_args],
            cwd=ROOT_DIR,
        )
        subprocess.check_call(
            ["cmake", "--build", str(build_dir), *build_args],
            cwd=ROOT_DIR,
        )

        candidates = list(build_dir.rglob("gameai_native*.so")) + list(build_dir.rglob("gameai_native*.pyd"))
        if not candidates:
            raise RuntimeError(f"Could not find built extension for {ext.name} under {build_dir}")

        shutil.copy2(candidates[0], ext_fullpath)


setup(
    packages=[],
    ext_modules=[CMakeExtension("gameai_native")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
)
