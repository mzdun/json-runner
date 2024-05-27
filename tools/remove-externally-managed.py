import os
import platform
import sys

maj, min, _ = platform.python_version_tuple()
path = f'{sys.exec_prefix}/lib/python{maj}.{min}/EXTERNALLY-MANAGED'
try:
    os.remove(path)
except FileNotFoundError:
    pass
