# Copyright (c) 2023 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import platform
import shlex


def _os_release_fallback():
    result = {}
    with open("/etc/os-release", encoding="UTF-8") as os_rel:
        for line in os_rel:
            line = line.strip()
            if line[:1] == "#":
                line = ""
            if line == "":
                continue
            name, value = line.split("=", 1)
            value = " ".join(shlex.split(value))
            result[name] = value
    return result


def uname():
    _platform = platform.uname()
    platform_name = _platform.system.lower()
    platform_version = _platform.version
    platform_arch = _platform.machine.lower()

    if platform_arch == "amd64":
        platform_arch = "x86_64"

    if platform_name == "linux":
        try:
            os_release = platform.freedesktop_os_release()
        except AttributeError:
            os_release = _os_release_fallback()

        platform_id = os_release.get("ID", os_release.get("NAME"))
        version_id = os_release.get("VERSION_ID", platform_version)
        if platform_id is not None:
            if platform_id[:9] == "opensuse-":
                platform_id = "opensuse"
            platform_name = platform_id.lower()
            if version_id != "" and version_id[0] in "0123456789":
                platform_version = ".".join(version_id.split(".", 2)[:2])

    return (
        platform_name,
        platform_version,
        platform_arch,
    )
