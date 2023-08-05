#!/usr/bin/env python3
# Copyright (c) 2023 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import os
import sys
import subprocess


__flow__ = os.path.abspath(os.path.join(os.path.dirname(__file__), "tools", "flow"))


def list_flows():
    for _, dirnames, filenames in os.walk(__flow__):
        dirnames[:] = []
        split = (os.path.splitext(filename) for filename in filenames)
        return list(
            sorted(name[0] for name in split if len(name) > 1 and name[1] == ".py")
        )
    return []


flows = list_flows()

if len(sys.argv) < 2 or sys.argv[1] in ["-h", "--help"]:
    print(f"{sys.argv[0]} ({' | '.join(flows)})")
    sys.exit(0)

if sys.argv[1] not in flows:
    print(f"{sys.argv[0]}: error: unknown flow `{sys.argv[1]}`", file=sys.stderr)
    sys.exit(0)

args = [sys.executable, os.path.join(__flow__, f"{sys.argv[1]}.py"), *sys.argv[2:]]
try:
    sys.exit(subprocess.run(args, shell=False).returncode)
except KeyboardInterrupt:
    sys.exit(1)
