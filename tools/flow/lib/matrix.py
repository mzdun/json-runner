# Copyright (c) 2023 Marcin Zdun
# This code is licensed under MIT license (see LICENSE for details)

import json
import os
import subprocess
import shutil
import sys
import textwrap
from typing import Dict, List, Optional, Tuple

from .runner import (
    copy_file,
    print_args,
    runner,
    step_call,
    step_info,
    conan,
    conan_api,
)
from .uname import uname

sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(__file__))))

from github.cmake import get_version

_conan: Optional[conan] = None
_platform_name, _platform_version, _platform_arch = uname()

_collect_version = (0, 21, 1)
_report_version = (0, 20, 0)

platform = _platform_name

_names = {
    "clang": ["clang", "clang++"],
    "stdclang": ["clang", "stdclang"],
    "gcc": ["gcc", "g++"],
}

_cpack_generators = {
    "ZIP": ".zip",
    "TGZ": ".tar.gz",
    "WIZ": ".msi",
}


def arch_ext(config: dict):
    cpack_generator = config.get("cpack_generator", [])
    if len(cpack_generator):
        try:
            return _cpack_generators[cpack_generator[0]]
        except KeyError:
            pass
    return None


def package_name_(config: dict, group: str, ext: str):
    debug = "-dbg" if config.get("build_type", "").lower() == "debug" else ""
    pkg = get_version().pkg()
    if ext is None:
        ext = ".zip"
    platform_with_version = (
        platform if platform == "windows" else f"{platform}-{_platform_version}"
    )
    if group != "":
        group = f"-{group}"
    return f"{pkg}-{platform_with_version}-{_platform_arch}{debug}{group}{ext}"


def package_name(config: dict, group: str):
    return package_name_(config, group, arch_ext(config))


def matches(tested: dict, test: dict):
    for key, value in test.items():
        val = tested.get(key)
        if val != value:
            return False
    return True


def matches_any(tested: dict, tests: list):
    for test in tests:
        if matches(tested, test):
            return True
    return False


def _split_keys(includes: List[dict], keys: List[str]) -> List[Tuple[dict, dict]]:
    result = []
    for include in includes:
        expand_key = {}
        expand_value = {}
        for key, value in include.items():
            if key in keys:
                expand_key[key] = value
            else:
                expand_value[key] = value
        result.append((expand_key, expand_value))
    return result


def cartesian(input: Dict[str, list]) -> List[dict]:
    product = [{}]

    for key, values in input.items():
        next_level = []
        for value in values:
            for obj in product:
                next_level.append({**obj, key: value})
        product = next_level

    return product


def load_matrix(*json_paths: str) -> Tuple[List[dict], List[str]]:
    setups: List[dict] = []
    for json_path in json_paths:
        with open(json_path, encoding="UTF-8") as f:
            setups.append(json.load(f))

    setup = setups[0]
    for additional in setups[1:]:
        src_matrix = setup.get("matrix", {})
        src_exclude = setup.get("exclude", [])
        src_include = setup.get("include", [])

        for key, value in additional.get("matrix", {}).items():
            old = src_matrix.get(key)
            if isinstance(old, list) and isinstance(value, list):
                old.extend(value)
            elif isinstance(old, list):
                old.append(value)
            else:
                src_matrix[key] = value
        src_exclude.extend(additional.get("exclude", []))
        src_include.extend(additional.get("include", []))

    raw = setup.get("matrix", {})
    keys = list(raw.keys())
    full = cartesian(raw)

    includes = _split_keys(setup.get("include", []), keys)
    for obj in full:
        for include_key, include_value in includes:
            if not matches(obj, include_key):
                continue
            for key, value in include_value.items():
                obj[key] = value

    excludes = setup.get("exclude", [])
    matrix = [obj for obj in full if not matches_any(obj, excludes)]
    if "NO_COVERAGE" in os.environ:
        for conf in matrix:
            if "coverage" in conf:
                del conf["coverage"]

    return matrix, keys


def find_compiler(compiler: str) -> Tuple[str, List[str]]:
    dirname = os.path.dirname(compiler)
    filename = os.path.basename(compiler)
    if sys.platform == "win32":
        filename = os.path.splitext(filename)[0]
    chunks = filename.split("-", 1)
    if len(chunks) == 1:
        version = None
    else:
        version = chunks[1]
    filename = chunks[0].lower()

    try:
        compiler_names = _names[filename]
    except:
        compiler_names = [filename]

    compilers = [
        os.path.join(dirname, name if version is None else f"{name}-{version}")
        for name in compiler_names
    ]

    if filename == "stdclang":
        filename = "clang"

    return filename, compilers


def _stats(coverage: List[Optional[int]]):
    total = 0
    relevant = 0
    covered = 0
    for count in coverage:
        total += 1
        if count is not None:
            relevant += 1
            if count != 0:
                covered += 1

    return total, relevant, covered


def _append_coverage(GITHUB_STEP_SUMMARY: str, coveralls: dict):
    total = 0
    relevant = 0
    covered = 0
    list_of_shame: List[Tuple[int, float, Optional[str], int, int, int]] = []

    for file in coveralls.get("source_files", []):
        stats_total, stats_relevant, stats_covered = _stats(file.get("coverage", []))
        if stats_relevant != stats_covered:
            list_of_shame.append(
                (
                    stats_relevant - stats_covered,
                    1.0 - stats_covered / stats_relevant,
                    file.get("name"),
                    stats_total,
                    stats_relevant,
                    stats_covered,
                )
            )
        total += stats_total
        relevant += stats_relevant
        covered += stats_covered

    list_of_shame.sort(reverse=True)
    list_of_shame = list_of_shame[:5]

    SLASH_LINE = " \\\n"
    SPAN_GREEN = '<span style="color: green">'
    SPAN_ORANGE = '<span style="color: orange">'
    SPAN_RED = '<span style="color: red">'
    SPAN_C = "</span>"

    with open(GITHUB_STEP_SUMMARY, "a", encoding="UTF-8") as out:
        percentage_100 = int(covered * 100_00 / relevant + 0.5)
        print(
            f"**Coverage:** _{percentage_100//100}.{percentage_100%100:02}% ({covered}/{relevant})_",
            end=SLASH_LINE,
            file=out,
        )
        print(
            f"**Missing lines:** _{relevant - covered}_",
            file=out,
        )
        if len(list_of_shame):
            print(
                f"\n|Name|Coverage|Covered|Relevant|{SPAN_RED}Missing{SPAN_C}|",
                file=out,
            )
            print("|----|-------:|------:|-------:|------:|", file=out)
        for M, ratio, name, _total, R, C in list_of_shame:
            P100 = int(C * 100_00 / R + 0.5)
            SPAN = (
                SPAN_GREEN
                if ratio <= 0.1
                else SPAN_ORANGE
                if ratio <= 0.25
                else SPAN_RED
            )
            code_name = "_unknown_" if name is None else f"`{name}`"
            print(
                f"|{code_name}|{SPAN}{P100//100}.{P100%100:02}%{SPAN_C}|{C}|{R}|{SPAN_RED}{M}{SPAN_C}|",
                file=out,
            )


class steps:
    @staticmethod
    @step_call("Conan")
    def configure_conan(config: dict):
        global _conan

        CONAN_DIR = "build/conan"
        CONAN_PROFILE = "_profile-compiler"
        CONAN_PROFILE_GEN = "_profile-build_type"
        profile_gen = f"./{CONAN_DIR}/{CONAN_PROFILE_GEN}-{config['preset']}"

        if config.get("--first-build"):
            runner.refresh_build_dir("conan")

        if _conan is None:
            _conan = conan_api()

        if not runner.DRY_RUN:
            with open(profile_gen, "w", encoding="UTF-8") as profile:
                print(
                    textwrap.dedent(
                        f"""\
                        include({CONAN_PROFILE})

                        [settings]"""
                    ),
                    file=profile,
                )

                for setting in [
                    *_conan.settings(config),
                    f"build_type={config['build_type']}",
                ]:
                    print(setting, file=profile)

        _conan.config(CONAN_DIR, f"./{CONAN_DIR}/{CONAN_PROFILE}", profile_gen)
        if not runner.DRY_RUN:
            os.remove("CMakeUserPresets.json")

    @staticmethod
    @step_call("CMake")
    def configure_cmake(config: dict):
        runner.refresh_build_dir(config["preset"])
        RUNNER_SANITIZE = "ON" if config.get("sanitizer") else "OFF"
        RUNNER_CUTDOWN_OS = "ON" if runner.CUTDOWN_OS else "OFF"
        runner.call(
            "cmake",
            "--preset",
            f"{config['preset']}-{config['build_generator']}",
            f"-DRUNNER_SANITIZE:BOOL={RUNNER_SANITIZE}",
            f"-DRUNNER_CUTDOWN_OS:BOOL={RUNNER_CUTDOWN_OS}",
        )

    @staticmethod
    @step_call("Build")
    def build(config: dict):
        runner.call("cmake", "--build", "--preset", config["preset"], "--parallel")

    @staticmethod
    def get_bin(version: Tuple[int], config: dict):
        ext = ".exe" if os.name == "nt" else ""
        for dirname in ["latest", "release", config["preset"]]:
            path = f"build/{dirname}/bin/runner{ext}"
            if not os.path.isfile(path):
                continue
            proc = subprocess.run([path, "--version"], shell=False, capture_output=True)
            if proc.returncode:
                continue
            exe_version = tuple(
                int(x)
                for x in proc.stdout.strip()
                .split(b" ")[-1]
                .split(b"-")[0]
                .decode("UTF-8")
                .split(".")
            )
            if exe_version >= version:
                return path
        return None

    @staticmethod
    @step_call("Test")
    def test(config: dict):
        has_coverage = config.get("coverage")
        cov_exe = None
        if has_coverage:
            cov_exe = steps.get_bin(_collect_version, config)

        if cov_exe is not None:
            runner.call(cov_exe, "collect", "--clean")
            runner.call(
                cov_exe, "collect", "--observe", "ctest", "--preset", config["preset"]
            )
            runner.call(cov_exe, "collect")
            # todo: report coverage to github, somehow
            return

        runner.call("ctest", "--preset", config["preset"])

    @staticmethod
    @step_call("Sign", lambda config: config.get("os") == "windows")
    def sign(config: dict):
        files_to_sign: List[str] = []
        for root_dir, _, files in os.walk(
            os.path.join("build", config["preset"], "bin")
        ):
            for filename in files:
                stem, ext = os.path.splitext(filename)
                if stem[-5:] == "-test" or ext.lower() not in [".exe", ".dll"]:
                    continue
                files_to_sign.append(
                    os.path.join(root_dir, filename).replace("\\", "/")
                )
        runner.call("python", "tools/win32/sign.py", *files_to_sign)

    @staticmethod
    @step_call("Pack", lambda config: len(config.get("cpack_generator", [])) > 0)
    def pack(config: dict):
        runner.call(
            "cpack",
            "--preset",
            config["preset"],
            "-G",
            ";".join(config.get("cpack_generator", [])),
        )

    @staticmethod
    @step_call(
        "SignPackages",
        lambda config: config.get("os") == "windows"
        and len(config.get("cpack_generator", [])) > 0,
    )
    def sign_packages(config: dict):
        files_to_sign: List[str] = []
        for root_dir, dirs, files in os.walk(
            os.path.join("build", config["preset"], "packages")
        ):
            dirs[:] = []
            for filename in files:
                _, ext = os.path.splitext(filename)
                if ext.lower() != ".msi":
                    continue
                files_to_sign.append(
                    os.path.join(root_dir, filename).replace("\\", "/")
                )
        runner.call("python", "tools/win32/sign.py", *files_to_sign)

    @staticmethod
    @step_call("Store")
    def store(config: dict):
        steps._store_packages(config)
        steps._store_tests(config)

    @staticmethod
    @step_call("StorePackages", flags=step_info.VERBOSE)
    def store_packages(config: dict):
        steps._store_packages(config)

    @staticmethod
    @step_call("StoreTests", flags=step_info.VERBOSE)
    def store_tests(config: dict):
        steps._store_tests(config)

    @staticmethod
    def _store_packages(config: dict):
        preset = config["preset"]
        packages_dir = f"build/{preset}/packages"
        if not runner.DRY_RUN:
            os.makedirs("build/artifacts", exist_ok=True)

        runner.copy(
            packages_dir,
            "build/artifacts/packages",
            r"^runner-.*$",
        )

        if runner.GITHUB_ANNOTATE:
            try:
                GITHUB_OUTPUT = os.environ["GITHUB_OUTPUT"]
                with open(GITHUB_OUTPUT, "a", encoding="UTF-8") as github_output:
                    generators = ",".join(config.get("cpack_generator", []))
                    print(f"CPACK_GENERATORS={generators}", file=github_output)
            except KeyError:
                pass

    @staticmethod
    def _store_tests(config: dict):
        preset = config["preset"]

        runner.copy(f"build/{preset}/test-results", "build/artifacts/test-results")
        if config.get("coverage"):
            output = f"build/artifacts/coveralls/{config['report_os']}-{config['report_compiler']}-{config['build_type']}.json"
            if os.path.exists(f"build/{preset}/coveralls.json"):
                print_args("cp", f"build/{preset}/coveralls.json", output)
                if not runner.DRY_RUN:
                    copy_file(f"build/{preset}/coveralls.json", output)
            else:
                print_args("cp", f"build/{preset}/collected.json", output)
                if not runner.DRY_RUN:
                    copy_file(f"build/{preset}/collected.json", output)

    @staticmethod
    @step_call(
        "BinInst",
        flags=step_info.VERBOSE,
        visible=lambda cfg: arch_ext(cfg) is not None,
    )
    def bin_inst(config: dict):
        if not runner.DRY_RUN:
            os.makedirs("build/.local", exist_ok=True)
        runner.extract(
            "build/artifacts/packages", "build/.local", package_name(config, "")
        )

    @staticmethod
    @step_call(
        "DevInst",
        flags=step_info.VERBOSE,
        visible=lambda cfg: arch_ext(cfg) is not None,
    )
    def dev_inst(config: dict):
        if not runner.DRY_RUN:
            os.makedirs("build/.user", exist_ok=True)
        runner.extract(
            "build/artifacts/packages", "build/.user", package_name(config, "devel")
        )

    @staticmethod
    @step_call(
        "Report",
        flags=step_info.VERBOSE,
        visible=lambda config: config.get("coverage") == True,
    )
    def report(config: dict):
        cov_exe = steps.get_bin(_collect_version, config)
        reporter = steps.get_bin(_report_version, config)

        try:
            tag_process = subprocess.run([reporter, "tag"], stdout=subprocess.PIPE)
            tags = (
                tag_process.stdout.decode("UTF-8")
                .replace("\r\n", "\n")
                .strip()
                .split("\n")
            )
        except:
            tags = ()
        version = get_version()
        report = f"build/{config['preset']}/collected.json"
        response = f"build/{config['preset']}/report_answers.txt"
        at_args = []
        if os.path.isfile(response):
            at_args.append(f"@{response}")
        if cov_exe is None:
            coveralls = f"build/{config['preset']}/coveralls.json"
            runner.call(
                reporter,
                "report",
                "--out",
                report,
                "--filter",
                "coveralls",
                coveralls,
            )
        runner.call(reporter, "report", "--filter", "strip-excludes", report, *at_args)
        if version.tag() in tags:
            runner.call(
                reporter,
                "show",
                "--format=oneline",
                "--abbrev-hash",
                f"{version.tag()}..",
            )

    @staticmethod
    def build_steps():
        return [
            steps.configure_conan,
            steps.configure_cmake,
            steps.build,
            steps.test,
            steps.report,
            steps.sign,
            steps.pack,
            steps.sign_packages,
            steps.store,
            steps.store_packages,
            steps.store_tests,
            steps.bin_inst,
            steps.dev_inst,
        ]

    @staticmethod
    def build_config(config: dict, keys: list, wanted_steps: list):
        program = steps.build_steps()
        use_step = (
            (lambda step: step(None).name.lower() in wanted_steps)
            if len(wanted_steps)
            else (lambda step: not step(None).only_verbose())
        )
        program = [step for step in program if use_step(step)]
        runner.run_steps(config, keys, program)
