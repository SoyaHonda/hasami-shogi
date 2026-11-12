"""Builds hasami_env._core from lib/core and python/hasami_env/_core.cpp.

    python python/setup.py build_ext --inplace   # what python/conftest.py runs before the tests
    pip install --no-build-isolation -e python   # or install it (editable) into the environment
"""

import glob
import importlib.util
import os

from pybind11.setup_helpers import ParallelCompile, Pybind11Extension, build_ext
from setuptools import setup

HERE = os.path.dirname(os.path.abspath(__file__))
# Package paths (and build_ext --inplace) are relative to the working directory.
os.chdir(HERE)
CORE = os.path.normpath(os.path.join(HERE, "..", "lib", "core"))

# Absolute paths: setuptools keeps the object files of sources outside this directory under
# build/ (relative paths with .. would put them next to the sources).
SOURCES = [os.path.join(HERE, "hasami_env", "_core.cpp")] + sorted(glob.glob(os.path.join(CORE, "*.cpp")))
HEADERS = sorted(glob.glob(os.path.join(CORE, "*.h")))

# The sources' hash, built into the module (hasami_env/srchash.py, loaded without the package).
_spec = importlib.util.spec_from_file_location("srchash", os.path.join(HERE, "hasami_env", "srchash.py"))
_srchash = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_srchash)
SOURCE_HASH = _srchash.source_hash(os.path.dirname(HERE))

ParallelCompile("NPY_NUM_BUILD_JOBS").install()

setup(
    name="hasami_env",
    version="0.1.0",
    packages=["hasami_env"],
    ext_modules=[
        Pybind11Extension(
            "hasami_env._core",
            SOURCES,
            include_dirs=[CORE],
            define_macros=[("HASAMI_SOURCE_HASH", '"%s"' % SOURCE_HASH)],
            depends=HEADERS,
            cxx_std=17,
            # The same floating point as the native, WASM and ESP32 builds (platformio.ini).
            extra_compile_args=["-O2", "-ffp-contract=off", "-pthread"],
            extra_link_args=["-pthread"],
        )
    ],
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    python_requires=">=3.9",
)
