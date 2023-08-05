#!/usr/bin/env python
# Copyright (c) 2023 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import zipfile
import tarfile
import os
from typing import Dict, Callable, Tuple, List


def _untar(src, dst):
    with tarfile.open(src) as TAR:

        def is_within_directory(directory, target):
            abs_directory = os.path.abspath(directory)
            abs_target = os.path.abspath(target)

            prefix = os.path.commonprefix([abs_directory, abs_target])

            return prefix == abs_directory

        for member in TAR.getmembers():
            member_path = os.path.join(dst, member.name)
            if not is_within_directory(dst, member_path):
                raise Exception(f"Attempted path traversal in Tar file: {member.name}")

        TAR.extractall(dst)


def _unzip(src, dst):
    with zipfile.ZipFile(src) as ZIP:
        ZIP.extractall(dst)


_tar = (_untar, ["tar", "-xf"])

Unpacker = Callable[[str, str], None]
UnpackInfo = Tuple[Unpacker, List[str]]

ARCHIVES: Dict[str, UnpackInfo] = {
    ".tar": _tar,
    ".tar.gz": _tar,
    ".zip": (_unzip, ["unzip"]),
}


def locate_unpack(archive: str) -> UnpackInfo:
    reminder, ext = os.path.splitext(archive)
    _, mid = os.path.splitext(reminder)
    if mid == ".tar":
        ext = ".tar"
    return ARCHIVES[ext]


del _tar
del _unzip
del _untar
