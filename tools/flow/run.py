#!/usr/bin/env python3
# Copyright (c) 2023 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import argparse
import json
import os
import sys
from contextlib import contextmanager
from typing import List, Set

import lib.matrix as matrix
import lib.runner as runner

DEF_STEPS = {
    "config": ["Conan", "CMake"],
    "build": ["Build"],
    "verify": [
        "Build",
        "Test",
        "Report",
        "Sign",
        "Pack",
        "SignPackages",
        "Store",
        "BinInst",
        "DevInst",
    ],
    "report": ["Build", "Test", "Report"],
}
cmd = os.path.splitext(os.path.basename(sys.argv[0]))[0]


@contextmanager
def cd(path):
    current = os.getcwd()
    try:
        os.chdir(path)
        yield
    finally:
        os.chdir(current)


_used_compilers = {}


def _compiler(value: str):
    global _used_compilers
    compiler, names = matrix.find_compiler(value)
    if compiler not in _used_compilers:
        _used_compilers[compiler] = []
    _used_compilers[compiler].append(names)
    return compiler


def _boolean(value: str):
    return value.lower() in _TRUE


_TRUE = {"true", "on", "yes", "1", "with-sanitizer"}
_TYPES = {"compiler": _compiler, "sanitizer": _boolean}


def _flatten(array: List[list]) -> list:
    return [item for sublist in array for item in sublist]


def _config(config: List[str], only_host: bool):
    args = {}
    for arg in config:
        if arg[:1] == "-":
            continue
        _arg = arg.split("=", 1)
        if len(_arg) == 1:
            continue

        name, vals = _arg
        name = name.strip()
        conv = _TYPES.get(name, lambda value: value)
        values = {conv(val.strip()) for val in vals.split(",")}
        if name in args:
            values.update(args[name])
        args[name] = list(values)
    if only_host and "os" not in args:
        args["os"] = [matrix.platform]
    return matrix.cartesian(args)


def _steps(steps: List[str], parser: argparse.ArgumentParser):
    result = [
        step.strip().lower() for step in _flatten(step.split(",") for step in steps)
    ]
    known = [step(None).name.lower() for step in matrix.steps.build_steps()]
    for step in result:
        if step not in known:
            names = ", ".join(step(None).name for step in matrix.steps.build_steps())
            parser.error(f"unrecognized step: {step}; known steps are: {names}")
    return result


def _extend(plus: List[str], pre_steps: List[str]):
    result: Set[str] = {
        info.name.lower()
        for info in (step(None) for step in matrix.steps.build_steps())
        if not info.only_verbose()
    }
    if len(pre_steps):
        result = set(pre_steps)
    result.update(plus)
    return list(result)


def _name(step: runner.step_info):
    return f"*{step.name}" if step.only_verbose() else step.name


def _known_steps():
    steps = matrix.steps.build_steps()
    if not len(steps):
        return "<none>"
    infos = [step(None) for step in steps]
    verbose = [1 for info in infos if info.only_verbose()]
    result = _name(infos[-1])
    prev_steps = ", ".join(_name(info) for info in infos[:-1])
    if len(prev_steps):
        result = f"{prev_steps} and {result}"
    if len(verbose):
        result = (
            f"{result} (note: steps with asterisks can only be called through --step)"
        )
    return result


_default_compiler = {"ubuntu": "gcc", "windows": "msvc"}
_ubuntu_lts = ["ubuntu-20.04", "ubuntu-22.04", "ubuntu-24.04"]


def default_compiler():
    try:
        return os.environ["DEV_CXX"]
    except KeyError:
        return _default_compiler[matrix.platform]


def _alias_cmds() -> str:
    try:
        cmds = DEF_STEPS[cmd]
    except KeyError:
        return ""
    return f". This alias runs: {', '.join(cmds)}."


parser = argparse.ArgumentParser(description="Unified action runner" + _alias_cmds())
parser.add_argument(
    "--dry-run",
    action="store_true",
    required=False,
    help="print steps and commands, do nothing",
)
group = parser.add_mutually_exclusive_group()
group.add_argument(
    "--dev",
    required=False,
    action="store_true",
    help=f'shortcut for "-c os={matrix.platform} '
    f'compiler={default_compiler()} build_type=Debug"',
)
group.add_argument(
    "--rel",
    required=False,
    action="store_true",
    help=f'shortcut for "-c os={matrix.platform} '
    f'compiler={default_compiler()} build_type=Release"',
)
group.add_argument(
    "--both",
    required=False,
    action="store_true",
    help=f'shortcut for "-c os={matrix.platform} '
    f'compiler={default_compiler()} build_type=Debug build_type=Release"',
)
parser.add_argument(
    "--cutdown-os",
    required=False,
    action="store_true",
    help="configure CMake with COV_CUTDOWN_OS=ON",
)
parser.add_argument(
    "--github",
    required=False,
    action="store_true",
    help="use ::group:: annotation in steps",
)
if cmd == "run":
    parser.add_argument(
        "-s",
        "--steps",
        metavar="step",
        nargs="*",
        action="append",
        default=[],
        help="run only listed steps; if missing, run all the steps; "
        f"known steps are: {_known_steps()}.",
    )
parser.add_argument(
    "-S",
    dest="steps_plus",
    nargs="*",
    action="append",
    default=[],
    help=argparse.SUPPRESS,
)
parser.add_argument(
    "-c",
    dest="configs",
    metavar="config",
    nargs="*",
    action="append",
    default=[],
    help="run only build matching the config; "
    "each filter is a name of a matrix axis followed by comma-separated values to take; "
    f'if "os" is missing, it will default to additional "-c os={matrix.platform}"',
)
parser.add_argument(
    "--matrix",
    action="store_true",
    required=False,
    help="print matrix json",
)
parser.add_argument(
    "--official",
    action="store_true",
    required=False,
    help="cut matrix to minimal set of builds",
)

def _turn_one(config: dict, github_os: str, os_in_name: str):
    config["github_os"] = github_os
    config["build_name"] = f"{config['build_type']} with {config['compiler']} on {os_in_name}"
    config["needs_gcc_ppa"] = config["os"] == "ubuntu" and int(github_os.split("-")[1].split(".")[0]) < 24
    return config

def _turn(config: dict, spread_lts: bool):
    if config["os"] == "ubuntu" and spread_lts:
        return [_turn_one({key: config[key] for key in config}, lts, lts) for lts in _ubuntu_lts]
    return [_turn_one(config, f"{config['os']}-latest", config["os"])]

def main():
    args = parser.parse_args()
    if "RELEASE" in os.environ and "GITHUB_ACTIONS" in os.environ:
        args.official = not not json.loads(os.environ["RELEASE"])
    if cmd != "run":
        args.steps = [step.lower() for step in DEF_STEPS[cmd]]
    else:
        args.steps = _steps(_flatten(args.steps), parser)
    args.steps_plus = _steps(_flatten(args.steps_plus), parser)
    if cmd == "run" and len(args.steps) and len(args.steps_plus):
        parser.error("-s/--step and -S are mutually exclusive")
    if len(args.steps_plus):
        args.steps = _extend(args.steps_plus, args.steps)
    if args.dev or args.rel or args.both:
        args.configs.append(
            [
                f"os={matrix.platform}",
                f"compiler={default_compiler()}",
                f"build_type={'Release' if args.rel else 'Debug'}",
            ]
        )
        if args.both:
            args.configs.append(["build_type=Release"])
    args.configs = _config(_flatten(args.configs), not (args.official or args.github))
    runner.runner.DRY_RUN = args.dry_run
    runner.runner.CUTDOWN_OS = args.cutdown_os
    runner.runner.GITHUB_ANNOTATE = args.github
    runner.runner.OFFICIAL = args.official
    root = os.path.join(os.path.dirname(__file__), "..", "..", ".github", "workflows")
    paths = [os.path.join(root, "flow.json")]
    if args.official:
        paths.append(os.path.join(root, "flow.official.json"))
    configs, keys = matrix.load_matrix(*paths)

    usable = [
        _turn(config, args.matrix)
        for config in configs
        if len(args.configs) == 0 or matrix.matches_any(config, args.configs)
    ]
    usable = [cfg for group in usable for cfg in group]

    if args.matrix:
        if "GITHUB_ACTIONS" in os.environ:
            var = json.dumps({"include": usable})
            print(f"matrix={var}")
        else:
            json.dump(usable, sys.stdout)
        return

    first_build = True
    for conf in usable:
        try:
            used_compilers = _used_compilers[conf["compiler"]]
        except KeyError:
            used_compilers = [matrix.find_compiler(conf["compiler"])[1]]
        for compiler in used_compilers:
            if not first_build:
                print()

            matrix.steps.build_config(
                {
                    **conf,
                    "compiler": compiler,
                    "--orig-compiler": conf["compiler"],
                    "--first-build": first_build,
                },
                keys,
                args.steps,
            )
            first_build = False


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(1)
