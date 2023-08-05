# Copyright (c) 2023 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import abc
import functools
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import zipfile
from contextlib import contextmanager
from dataclasses import dataclass
from typing import Callable, ClassVar, Dict, List, Optional, Tuple

__file_dir__ = os.path.dirname(__file__)
sys.path.append(os.path.dirname(os.path.dirname(__file_dir__)))

from archives import locate_unpack


def copy_file(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst, follow_symlinks=False)


@contextmanager
def _set_env(compiler: List[str]):
    orig_env = {}
    if sys.platform != "win32":
        for var, value in zip(["CC", "CXX"], compiler):
            if var in os.environ:
                orig_env[var] = os.environ[var]
            os.environ[var] = value
    try:
        yield
    finally:
        for var in ["CC", "CXX"]:
            if var in os.environ:
                del os.environ[var]
        for var, value in orig_env.items():
            os.environ[var] = value


def _prn1(value):
    if isinstance(value, str):
        return f'\033[2;31m"{value}"\033[m'
    return _prn2(value)


def _prn2(value):
    if isinstance(value, bool):
        return "\033[34mtrue\033[m" if value else "\033[34mfalse\033[m"
    if isinstance(value, str):
        return value
    if isinstance(value, dict):
        return "{{{}}}".format(", ".join(f"{key}: {_prn1(sub)}" for key, sub in value))
    if isinstance(value, (list, set, tuple)):
        return "[{}]".format(", ".join(_prn1(sub) for sub in value))
    return f"\033[2;34m{value}\033[m"


def _prn(config, indent=""):
    prefix = "- "
    for key, value in config.items():
        intro = f"{indent}{prefix}\033[2;37m{key}:\033[m"
        prefix = "  "
        if key[:2] == "--":
            continue
        if key == "compiler":
            comp = config["--orig-compiler"]
            if len(value) == 1 and value[0] == comp:
                print(f"{intro} {_prn2(comp)}", file=sys.stderr)
            else:
                print(f"{intro} {_prn2(comp)}, {_prn2(value)}", file=sys.stderr)
            continue
        print(f"{intro} {_prn2(value)}", file=sys.stderr)


def _sanitizer_str(value: bool):
    return "with-sanitizer" if value else "no-sanitizer"


def _build_name(config: dict, keys: list):
    name = []
    for key in keys:
        val = config[key]
        if key == "sanitizer":
            val = _sanitizer_str(val)
        elif key == "compiler":
            val = config[key][0]
        else:
            val = f"{val}"
        name.append(val)
    return ", ".join(name)


def _print_arg(arg: str):
    color = ""
    if arg[:1] == "-":
        color = "\033[2;37m"
    arg = shlex.join([arg])
    if color == "" and arg[:1] in ["'", '"']:
        color = "\033[2;34m"
    if color == "":
        return arg
    return f"{color}{arg}\033[m"


_print_prefix = ""


def print_args(*args: str):
    cmd = shlex.join([args[0]])
    args = " ".join([_print_arg(arg) for arg in args[1:]])
    print(f"{_print_prefix}\033[33m{cmd}\033[m {args}", file=sys.stderr)


def _ls(dirname):
    result = []
    for root, _, filenames in os.walk(dirname):
        result.extend(
            os.path.relpath(os.path.join(root, filename), start=dirname)
            for filename in filenames
        )
    return result


class conan(abc.ABC):
    def __init__(self, version: int = 1):
        self.version = version

    def settings(self, cfg: dict):
        result: List[str] = []
        for threshold, name in enumerate(
            ["conan_settings", "conan2_settings"],
        ):
            if self.version <= threshold:
                break
            result = cfg.get(name, result)
        return result

    @abc.abstractmethod
    def config(
        self,
        conan_output_dir: str,
        compiler_profile_name: str,
        build_type_profile_name: str,
    ):
        ...


def _conan_version():
    found = shutil.which("conan")
    if found is None:
        return 1

    p = subprocess.run([found, "--version"], check=False, stdout=subprocess.PIPE)
    if p.returncode != 0:
        return 1
    conan_ver = p.stdout.decode("UTF-8").strip().split(" ")
    if len(conan_ver) <= 2:
        return 1

    conan_ver = conan_ver[2].split(".")
    try:
        return int(conan_ver[0])
    except ValueError:
        pass

    return 1


class conan_1(conan):
    def __init__(self):
        super().__init__(1)

    def config(
        self,
        conan_output_dir: str,
        compiler_profile_name: str,
        build_type_profile_name: str,
    ):
        runner.call(
            "conan",
            "profile",
            "new",
            "--detect",
            "--force",
            compiler_profile_name,
        )

        runner.call(
            "conan",
            "install",
            "-if",
            conan_output_dir,
            "-of",
            conan_output_dir,
            "--build",
            "missing",
            "-pr:b",
            build_type_profile_name,
            "-pr:h",
            build_type_profile_name,
            ".",
        )


class conan_2(conan):
    def __init__(self):
        super().__init__(2)

    def config(
        self,
        conan_output_dir: str,
        compiler_profile_name: str,
        build_type_profile_name: str,
    ):
        runner.call(
            "conan",
            "profile",
            "detect",
            "--force",
            "--name",
            compiler_profile_name,
        )

        runner.call(
            "conan",
            "install",
            "-of",
            conan_output_dir,
            "--build",
            "missing",
            "-pr:b",
            build_type_profile_name,
            "-pr:h",
            build_type_profile_name,
            ".",
        )


def conan_api() -> conan:
    version = _conan_version()
    if version == 2:
        return conan_2()
    return conan_1()


@dataclass
class step_info:
    VERBOSE: ClassVar[int] = 1

    name: str
    impl: Callable[[dict], None]
    visible: Optional[Callable[[dict], bool]]
    flags: int

    def only_verbose(self):
        return self.flags & step_info.VERBOSE == step_info.VERBOSE


class runner:
    DRY_RUN = False
    CUTDOWN_OS = False
    GITHUB_ANNOTATE = False
    OFFICIAL = False

    @staticmethod
    def run_steps(config: dict, keys: list, steps: List[callable]):
        title = _build_name(config, keys)
        prefix = "\033[1;34m|\033[m "
        first_step = True

        if not runner.GITHUB_ANNOTATE:
            print(f"\033[1;34m+--[BUILD] {title}\033[m", file=sys.stderr)
        if runner.DRY_RUN:
            _prn(config, prefix)
            first_step = False

        with _set_env(config["compiler"]):
            total = len(steps)
            counter = 0
            for step in steps:
                counter += 1
                if step(config, counter, total, prefix, first_step):
                    first_step = False

        if not runner.GITHUB_ANNOTATE:
            print(f"\033[1;34m+--------- \033[2;34m{title}\033[m", file=sys.stderr)

    @staticmethod
    def run_step(
        step: step_info,
        config: Optional[dict],
        counter: int,
        total: int,
        prefix: str,
        first_step: bool,
    ):
        global _print_prefix
        if config is None:
            return step

        if step.visible and not step.visible(config):
            return False

        if not first_step and not runner.GITHUB_ANNOTATE:
            print(prefix, file=sys.stderr)

        if runner.GITHUB_ANNOTATE:
            print(f"::group::STEP {counter}/{total}: {step.name}", file=sys.stderr)
        else:
            total_s = f"{total}"
            counter_s = f"{counter}"
            counter_pre = " " * (len(total_s) - len(counter_s))
            counter_s = f"{counter_pre}{counter_s}/{total_s}"

            print(
                f"{prefix}\033[1;35m+-[STEP] {counter_s} {step.name}\033[m",
                file=sys.stderr,
            )
            _print_prefix = f"{prefix}\033[1;35m|\033[m "
        step.impl(config)
        if runner.GITHUB_ANNOTATE:
            print(f"::endgroup::", file=sys.stderr)
        else:
            print(
                f"{prefix}\033[1;35m+------- \033[2;35m{counter_s} {step.name}\033[m",
                file=sys.stderr,
            )
        return True

    @staticmethod
    def refresh_build_dir(name):
        if runner.DRY_RUN:
            return

        fullname = os.path.join("build", name)
        shutil.rmtree(fullname, ignore_errors=True)
        os.makedirs(fullname, exist_ok=True)

    @staticmethod
    def call(*args, **kwargs):
        print_args(*args)
        if runner.DRY_RUN:
            return
        env_kwarg = kwargs.get("env", {})
        env = {**os.environ}
        for key, value in env_kwarg.items():
            if value is None:
                if key in env:
                    del env[key]
            else:
                env[key] = value
        found = shutil.which(args[0])
        args = (found if found is not None else args[0], *args[1:])
        proc = subprocess.run(args, check=False, env=env)
        if proc.returncode != 0:
            sys.exit(1)

    @staticmethod
    def copy(src_dir: str, dst_dir: str, regex: str = ""):
        print_args("cp", "-r", src_dir, dst_dir)
        if runner.DRY_RUN:
            return
        files = _ls(src_dir)
        if regex:
            files = (name for name in files if re.match(regex, name))
        for name in files:
            copy_file(os.path.join(src_dir, name), os.path.join(dst_dir, name))

    @staticmethod
    def extract(src_dir: str, dst_dir: str, package: str):
        archive = f"{src_dir}/{package}"

        unpack, msg = locate_unpack(archive)

        print_args(*msg, archive, dst_dir)
        if not runner.DRY_RUN:
            unpack(archive, dst_dir)


def step_call(
    step_name: str, visible: Optional[Callable[[dict], bool]] = None, flags: int = 0
):
    def decorator(step: Callable[[dict], None]):
        @functools.wraps(step)
        def run_step(
            config: Optional[dict],
            counter: int = 0,
            total: int = 0,
            prefix: str = "",
            first_step=True,
        ):
            info = step_info(name=step_name, impl=step, visible=visible, flags=flags)
            return runner.run_step(info, config, counter, total, prefix, first_step)

        return run_step

    return decorator
