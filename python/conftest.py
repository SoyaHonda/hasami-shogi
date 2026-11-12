"""Builds hasami_env._core in place before the tests (python -m pytest python).

setuptools rebuilds only when a source or a header of lib/core is newer than the module, so an
up-to-date build costs about a second. HASAMI_ENV_NO_BUILD=1 skips it and tests the hasami_env
installed in the environment (a wheel), run with --import-mode=importlib so that the source
directory does not shadow it.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

if os.environ.get("HASAMI_ENV_NO_BUILD") != "1":
    subprocess.run([sys.executable, os.path.join(HERE, "setup.py"), "-q", "build_ext", "--inplace"], cwd=HERE, check=True)
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
