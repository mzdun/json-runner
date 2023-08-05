#!/usr/bin/env python3
# Copyright (c) 2023 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import argparse
import platform
import shlex
from pprint import pprint

parser = argparse.ArgumentParser()
parser.add_argument("format", choices=["props", "platform", "debug"])
args = parser.parse_args()


def _os_release():
    for file in ["/etc/os-release", "/usr/lib/os-release"]:
        try:
            result = {}
            with open(file) as f:
                for line in f:
                    var = line.strip().split("=", 1)
                    if len(var) < 2:
                        continue
                    name, value = (val.strip() for val in var)
                    value = " ".join(shlex.split(value))
                    result[name] = value
            return result
        except FileNotFoundError:
            pass
        return {}


node = platform.node()
uname = platform.uname()

system = uname.system.lower()
machine = uname.machine
version = uname.version

if args.format == "debug":
    for name in [
        "uname",
        "machine",
        "node",
        "platform",
        "processor",
        "release",
        "system",
        "version",
    ]:
        print(name, platform.__dict__[name]())

system_nt = system.split("_nt-", 1)
if len(system_nt) > 1:
    system = system_nt[0]
    version = None
elif system == "windows":
    machine = "x86_64" if machine.lower() == "amd64" else "x86_32"
    version = None
elif system == "linux":
    os_release = _os_release()
    system = os_release.get("ID", system)
    version = os_release.get("VERSION_ID", version)

    if system[:9] == "opensuse-":
        system = "opensuse"
    if system == "arch":
        version = None

    if args.format == "debug":
        pprint(os_release)


if args.format == "props":
    print(f"-phost.name='{node}'")
    print(f"-pos='{system}'")
    if version is not None:
        print(f"-pos.version='{version}'")
    print(f"-parch='{machine}'")

if args.format == "platform":
    version = "" if version is None else f"-{version}"
    print(f"{system}{version}-{machine}")

if args.format == "debug":
    print("-----")
    print(f"node {node}")
    print(f"os {system}")
    if version is not None:
        print(f"version {version}")
    print(f"machine {machine}")
