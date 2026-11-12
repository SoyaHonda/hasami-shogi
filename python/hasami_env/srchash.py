"""The hash of the C++ sources hasami_env._core is built from (lib/core and _core.cpp).

setup.py builds it into the module as SOURCE_HASH; the tests compare it with the checkout, so that
a prebuilt wheel older than the sources fails instead of testing old code. No import of _core here
(setup.py loads this file before the module exists).
"""

import glob
import hashlib
import os


def source_files(repo_root):
    core = os.path.join(repo_root, "lib", "core")
    files = sorted(glob.glob(os.path.join(core, "*.cpp")) + glob.glob(os.path.join(core, "*.h")))
    return files + [os.path.join(repo_root, "python", "hasami_env", "_core.cpp")]


def source_hash(repo_root):
    """sha256 over the file names and contents (line endings as LF), first 16 hex digits."""
    h = hashlib.sha256()
    for path in source_files(repo_root):
        with open(path, "rb") as f:
            data = f.read().replace(b"\r\n", b"\n")
        h.update(os.path.basename(path).encode() + b"\0" + str(len(data)).encode() + b"\0" + data)
    return h.hexdigest()[:16]
