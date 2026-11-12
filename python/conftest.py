"""Builds hasami_env._core in place before the tests (python -m pytest python).

setuptools rebuilds only when a source or a header of lib/core is newer than the module, so an
up-to-date build costs about a second. Without a C++ compiler or Python.h (the course's Azure VM:
no sudo), or with HASAMI_ENV_NO_BUILD=1, nothing is built and the hasami_env installed in the
environment (python/wheels/*.whl) is tested; test_build_matches_sources then fails if that build is
older than the sources.
"""

import os
import shutil
import subprocess
import sys
import sysconfig

HERE = os.path.dirname(os.path.abspath(__file__))


def _can_build():
    cxx = (sysconfig.get_config_var("CXX") or "c++").split()[0]
    header = os.path.join(sysconfig.get_paths()["include"], "Python.h")
    return shutil.which(cxx) is not None and os.path.exists(header)


BUILD = os.environ.get("HASAMI_ENV_NO_BUILD") != "1" and _can_build()
if BUILD:
    subprocess.run([sys.executable, os.path.join(HERE, "setup.py"), "-q", "build_ext", "--inplace"], cwd=HERE, check=True)
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
elif not os.path.exists(os.path.join(HERE, "hasami_env", "_core" + sysconfig.get_config_var("EXT_SUFFIX"))):
    # No module built here for this Python: the source directory must not hide the installed one.
    sys.path[:] = [p for p in sys.path if os.path.abspath(p or os.curdir) != HERE]


def pytest_report_header(config):
    try:
        import hasami_env

        where = os.path.dirname(hasami_env.__file__)
        return f"hasami_env: {'built in place' if BUILD else 'not built here'}, from {where}, SOURCE_HASH {hasami_env.SOURCE_HASH}"
    except ImportError as e:
        return f"hasami_env: cannot import ({e}); install python/wheels/*.whl or a C++ compiler and Python.h"
