// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#define NOMINMAX

#include <fmt/format.h>
#include <args/parser.hpp>
#include <filesystem>
#include <future>
#include <io/file.hh>
#include <io/run.hh>
#include <iostream>
#include <json/json.hpp>
#include <map>
#include <mt/thread_pool.hh>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>
#include "base/cmake.hh"
#include "base/shell.hh"
#include "base/str.hh"
#include "chai.hh"
#include "io/presets.hh"
#include "testbed/test.hh"
#include "version.hh"

using namespace std::literals;

namespace {
	class return_category : public std::error_category {
	public:
		const char* name() const noexcept override { return "return_category"; }

		std::string message(int value) const override {
			return fmt::format("Application returned {}.", value);
		}
	};
}  // namespace

std::error_category const& category() {
	static return_category cat;
	return cat;
}

std::error_code make_return_code(int return_code) {
	return std::error_code(return_code, category());
}

std::error_code install(
    fs::path const& copy_dir,
    fs::path const& binary_dir,
    std::string const& CMAKE_BUILD_TYPE,
    testbed::runtime& rt,
    std::vector<std::string> const& components,
    std::function<void(std::string const&, testbed::runtime&)> const&
        additional_install) {
	std::error_code ec{};

#define FS(NAME, ARGS)                                                      \
	fs::NAME ARGS;                                                          \
	if (ec) {                                                               \
		fmt::print("{}: error: {}, {}\n", #NAME, ec.value(), ec.message()); \
		return ec;                                                          \
	}
	FS(remove_all, (copy_dir, ec));

	FS(create_directories, (copy_dir, ec));

	{
		io::args_storage cmake{.stg{"--install", shell::get_path(binary_dir),
		                            "--config", CMAKE_BUILD_TYPE, "--prefix",
		                            shell::get_path(copy_dir)}};
		if (components.empty()) {
			std::string debug{};
			auto proc = io::run({.exec = "cmake",
			                     .args = cmake.args(),
			                     .output = io::devnull{},
			                     .debug = &debug});
			fputs(debug.c_str(), stdout);
			if (proc.return_code) return make_return_code(proc.return_code);
		} else {
			cmake.stg.push_back("--component");
			cmake.stg.push_back({});
			for (auto const& component : components) {
				std::string debug{};
				cmake.stg.back() = component;
				auto proc = io::run({.exec = "cmake",
				                     .args = cmake.args(),
				                     .output = io::devnull{},
				                     .debug = &debug});
				fputs(debug.c_str(), stdout);
				if (proc.return_code) return make_return_code(proc.return_code);
			}
		}
	}

	rt.rt_target = copy_dir / "bin"sv / rt.target.filename();

	if (!additional_install) return {};

	try {
		additional_install(shell::get_path(copy_dir), rt);
	} catch (std::error_code const& ec) {
		fmt::print("exception: {}, {}\n", ec.value(), ec.message());
		return ec;
	} catch (fs::filesystem_error const& fs_error) {
		fmt::print("exception: {}, {}\n", fs_error.code().value(),
		           fs_error.code().message());
		return fs_error.code();
	}

	return {};
}

struct color {
	std::string_view value;
	explicit constexpr color(std::string_view value) : value{value} {}

	static color const reset;
	static color const counter;
	static color const name;
	static color const failed;
	static color const passed;
	static color const skipped;
};

constinit color const color::reset{"\033[m"sv};
constinit color const color::counter{"\033[2;49;92m"sv};
constinit color const color::name{"\033[0;49;90m"sv};
constinit color const color::failed{"\033[0;49;91m"sv};
constinit color const color::passed{"\033[2;49;92m"sv};
constinit color const color::skipped{"\033[0;49;34m"sv};

template <>
struct fmt::formatter<color> : fmt::formatter<fmt::string_view> {
	auto format(color const& clr, fmt::format_context& ctx) const {
		return fmt::formatter<fmt::string_view>::format(
		    fmt::string_view{clr.value.data(), clr.value.length()}, ctx);
	}
};

class counters {
public:
	void report(outcome outcome,
	            std::string_view test_ident,
	            std::string_view message,
	            std::string_view prepare,
	            bool debug);

	bool summary(size_t counter) const;

private:
	unsigned error_{0};
	unsigned skip_{0};
	unsigned save_{0};
	std::vector<std::string> echo_{};
};

void counters::report(outcome result,
                      std::string_view test_ident,
                      std::string_view message,
                      std::string_view prepare,
                      bool debug) {
	switch (result) {
		case outcome::SKIPPED:
			if (debug) fmt::print("{}", prepare);
			fmt::print("{test_id} {color}SKIPPED{reset}\n",
			           fmt::arg("test_id", test_ident),
			           fmt::arg("color", color::skipped),
			           fmt::arg("reset", color::reset));
			++skip_;
			return;
		case outcome::SAVED:
			if (debug) fmt::print("{}", prepare);
			fmt::print("{test_id} {color}saved{reset}\n",
			           fmt::arg("test_id", test_ident),
			           fmt::arg("color", color::skipped),
			           fmt::arg("reset", color::reset));
			++skip_;
			++save_;
			return;
		case outcome::CLIP_FAILED: {
			fmt::print("{}", prepare);
			auto msg = fmt::format(
			    "{test_id} {color}FAILED (unknown check '{message}'){reset}",
			    fmt::arg("test_id", test_ident), fmt::arg("message", message),
			    fmt::arg("color", color::failed),
			    fmt::arg("reset", color::reset));
			fmt::print("{}\n", msg);
			echo_.push_back(msg);
			++error_;
			return;
		}
		case outcome::FAILED: {
			fmt::print("{}", prepare);
			if (!message.empty()) fmt::print("{}\n", message);
			auto msg = fmt::format("{test_id} {color}FAILED{reset}",
			                       fmt::arg("test_id", test_ident),
			                       fmt::arg("message", message),
			                       fmt::arg("color", color::failed),
			                       fmt::arg("reset", color::reset));
			fmt::print("{}\n", msg);
			echo_.push_back(msg);
			++error_;
			return;
		}
		case outcome::OK:
			if (debug) fmt::print("{}", prepare);
			fmt::print("{test_id} {color}PASSED{reset}\n",
			           fmt::arg("test_id", test_ident),
			           fmt::arg("color", color::passed),
			           fmt::arg("reset", color::reset));
			return;
	}
}

bool counters::summary(size_t counter) const {
	fmt::print("Failed {}/{}\n", error_, counter);
	if (skip_ != 0) {
		auto const test_s = skip_ == 1 ? "test"sv : "tests"sv;
		if (save_ != 0) {
			fmt::print("Skipped {} {} (including {} due to saving)\n", skip_,
			           test_s, save_);
		} else {
			fmt::print("Skipped {} {}\n", skip_, test_s);
		}
	}

	if (!echo_.empty()) fmt::print("\n");
	for (auto const& line : echo_)
		fmt::print("{}\n", line);

	return error_ == 0;
}

json::node to_lines(std::string_view text) {
	auto lines = split_str(text, '\n');
	if (lines.size() > 1 && lines.back().empty()) {
		lines.pop_back();
		lines.back().push_back('\n');
	}
	if (lines.size() == 1) return to_u8s(lines.front());
	json::array result{};
	result.reserve(lines.size());
	for (auto const& line : lines) {
		result.push_back(to_u8s(line));
	}
	return {std::move(result)};
}

static inline std::string painted(color clr, std::string_view label) {
	return fmt::format("{}{}{}", clr, label, color::reset);
};

test_results run_test2(testbed::test& tested,
                       std::map<std::string, std::string> const& variables,
                       testbed::runtime const& rt) {
	auto copy = rt;
	copy.temp_dir = rt.temp_dir / random_letters(16);

	auto test_ident = fmt::format(
	    "{} {}",
	    painted(color::counter,
	            fmt::format("[{:>{}}/{}]", tested.index, copy.counter_digits,
	                        copy.counter_total)),
	    painted(color::name, tested.name));

	fmt::print("{}\n", test_ident);
	auto actual = tested.run(variables, copy);

	if (!actual.capture) {
		return {outcome::SKIPPED, std::move(test_ident), copy.temp_dir,
		        std::move(actual.prepare)};
	}

	if (!tested.expected) {
		// TODO: store the new expected
		tested.data.set(u8"expected",
		                json::array{actual.capture->return_code,
		                            to_lines(actual.capture->output),
		                            to_lines(actual.capture->error)});
		tested.store();
		return {outcome::SAVED, std::move(test_ident), copy.temp_dir,
		        std::move(actual.prepare)};
	}

	auto clipped = tested.clip(*actual.capture);

	if ((*actual.capture == *tested.expected) ||
	    (clipped == *tested.expected)) {
		return {outcome::OK, std::move(test_ident), copy.temp_dir,
		        std::move(actual.prepare)};
	}

	return {outcome::FAILED, std::move(test_ident), copy.temp_dir,
	        std::move(actual.prepare), tested.report(clipped, copy)};
}

test_results run_test(testbed::test& tested,
                      std::map<std::string, std::string> const& variables,
                      testbed::runtime const& rt) {
	try {
		return run_test2(tested, variables, rt);
	} catch (std::exception const& e) {
		std::cerr << "exception: " << e.what() << '\n';
		throw;
	} catch (...) {
		throw;
	}
}

std::packaged_task<test_results()> package_test(
    testbed::test& tested,
    std::map<std::string, std::string> const& variables,
    testbed::runtime const& rt) {
	return std::packaged_task<test_results()>{
	    [&] { return run_test(tested, variables, rt); }};
}

int tool(::args::args_view const& args) {
#if 0
	{
		std::string dummy;
		fmt::print("Press enter when profiler's ready: ");
		std::getline(std::cin, dummy);
	}
#endif
	Chai chai;
	Chai::ProjectInfo info{};
	fs::path test_dir, copy_dir, binary_dir, test_set_dir;
	std::vector<size_t> run;
	std::string CMAKE_BUILD_TYPE;
	bool debug{false}, nullify{false}, keep_dirs{false};
	std::optional<std::string> lang{};
	std::optional<std::string> schema{};
	{
		std::string preset;
		std::string tests;

		::args::null_translator tr{};
		::args::parser p{{}, args, &tr};
		p.custom(
		     [&] {
			     fmt::print("{} version {}{}", p.program(), version::string,
			                version::stability);
			     if (!version::build_meta.empty())
				     fmt::print(" ({})", version::build_meta);
			     fmt::print("\n");
			     std::exit(0);
		     },
		     "version")
		    .opt()
		    .help("show version information and exit");
		p.arg(preset, "preset")
		    .meta("CONFIG")
		    .help("set name of CMake build preset");
		p.arg(tests, "tests")
		    .meta("DIR")
		    .help(
		        "point to directory with the JSON test cases; "
		        "test cases are enumerated recursively");
		p.arg(run, "run").meta("ID").opt().help("filter the tests to run");
		p.set<std::true_type>(debug, "debug")
		    .opt()
		    .help("print output even for successful tests");
		p.set<std::true_type>(nullify, "nullify")
		    .opt()
		    .help("set the \"expected\" field of the test cases to null");
		p.arg(lang, "lang")
		    .meta("ID")
		    .opt()
		    .help("change language for nullified tests");
		p.set<std::true_type>(keep_dirs, "keep-dirs")
		    .opt()
		    .help("keep directories created during this run");
		p.arg(schema, "schema")
		    .meta("URL")
		    .opt()
		    .help("update the \"$schema\" in files");
		p.parse();

		info = chai.project();
		test_dir = fs::weakly_canonical(info.datasets_dir);
		copy_dir = fs::weakly_canonical(u8"build/.json-runner"sv);

		auto const presets =
		    io::cmake::preset::load_file(u8"CMakePresets.json"sv);
		auto it = presets.find(preset);
		if (it == presets.end()) {
			p.error(fmt::format("preset `{}` is not found\n", preset));
		}
		auto bin_dir = it->second.get_binary_dir(presets);
		if (!bin_dir) {
			p.error(fmt::format("preset `{}` has no binaryDir attached to it\n",
			                    preset));
		}
		binary_dir = *bin_dir;

		auto build_type = it->second.get_build_type(presets);
		if (!build_type) {
			p.error(fmt::format(
			    "preset `{}` has no CMAKE_BUILD_TYPE attached to it\n",
			    preset));
		}
		CMAKE_BUILD_TYPE = *build_type;

		if (info.default_dataset) {
			auto const tests_dir = shell::make_u8path(tests);

			// apps\tests\main-set\001-cov
			if (!fs::is_directory(test_dir / tests_dir) &&
			    fs::is_directory(test_dir /
			                     shell::make_u8path(*info.default_dataset) /
			                     tests_dir)) {
				tests = fmt::format("{}/{}", *info.default_dataset, tests);
			}
		}

		test_set_dir = test_dir / shell::make_u8path(tests);
		test_set_dir.make_preferred();
	}

	auto const ext =
#ifdef _WIN32
	    ".exe"
#endif
	    ""s;

	auto const target = binary_dir / "bin"sv / (info.target + ext);
	if (!fs::is_regular_file(target)) {
		fmt::print(stderr, "cannot find {} in `{}`\n", info.target,
		           shell::get_path(binary_dir / "bin"));
		return 1;
	}

	std::vector<testbed::test> tests{};
	size_t unfiltered_count{};

	{
		fs::recursive_directory_iterator dir{test_set_dir};
		for (auto const& entry : dir) {
			if (entry.path().extension() != ".json"sv) continue;
			++unfiltered_count;
			if (!run.empty() && std::find(run.begin(), run.end(),
			                              unfiltered_count) == run.end())
				continue;
			auto test =
			    testbed::test::load(entry.path(), unfiltered_count, schema);
			if (!test.ok) continue;
			if (nullify) {
				test.nullify(lang);
				continue;
			}
			tests.push_back(std::move(test));
		}
		if (nullify) return 0;
	}

	if (tests.empty()) {
		fmt::print(stderr, "No tests to run.\n");
		return 0;
	}

	auto variables = shell::get_env();
	testbed::runtime rt{.target{target},
	                    .build_dir = binary_dir,
	                    .temp_dir = fs::canonical(fs::temp_directory_path()) /
	                                "json-test-runner",
	                    .version = cmake::get_project().ver(),
	                    .counter_total = unfiltered_count,
	                    .handlers = info.handlers(),
	                    .variables = &variables,
	                    .chai_variables = &info.environment,
	                    .common_patches = &info.common_patches,
	                    .debug = debug};
	auto ec = install(copy_dir, binary_dir, CMAKE_BUILD_TYPE, rt,
	                  info.install_components, info.installer);
	if (ec) {
		std::cerr << "error: " << ec.value() << ", " << ec.message() << '\n';
		return 1;
	}

	size_t label_size = 10;
	for (auto const& [var, _] : info.environment) {
		auto const len = var.size() + 1;
		if (len > label_size) label_size = len;
	}
	auto const mk_label = [label_size](std::string_view label,
	                                   std::string_view prefix = {}) {
		return fmt::format("{}{}:{:{}}", prefix, label, ' ',
		                   label_size + 1 - (label.size() + prefix.size()));
	};
	fmt::print("{}{} {}\n", mk_label("target"sv), shell::get_path(rt.target),
	           rt.version);
	fmt::print("{}{}\n", mk_label("tests"sv), shell::get_path(test_set_dir));
	for (auto const& [env, var] : info.environment)
		fmt::print("{}{}\n", mk_label(env, "$"sv), var);
	fmt::print("{}{}\n", mk_label("$INST"sv),
	           shell::get_path(rt.rt_target.parent_path().parent_path()));
	fmt::print("{}{}\n", mk_label("$TMP"sv), shell::get_path(rt.temp_dir));
	// fmt::print("{}{}\n", mk_label("JSON horiz"sv),
	// testbed::test::HORIZ_SPACE);
	fmt::print("common patches:\n");
	for (auto const& [expr, replacement] : info.common_patches)
		fmt::print("  {}: {},\n", repr(expr), repr(replacement));

	::counters counters{};

	auto const RUN_LINEAR = [&variables] {
		auto it = variables.find("RUN_LINEAR");
		return it != variables.end() && it->second != "0"sv;
	}();

	if (!RUN_LINEAR) {
		mt::thread_pool pool{};
		std::vector<std::future<test_results>> results{};

		results.reserve(tests.size());

		fmt::print("\nrunning parallel....\n");

		for (auto& test : tests) {
			if (test.linear) continue;
			auto task = package_test(test, variables, rt);
			results.emplace_back(task.get_future());
			pool.push(std::move(task));
		}

		for (auto& future : results) {
			auto results = future.get();
			counters.report(results.result, results.task_ident,
			                results.report ? *results.report : ""sv,
			                results.prepare, rt.debug);
			if (!keep_dirs) {
				std::error_code ignore{};
				fs::remove_all(results.temp_dir, ignore);
			} else {
				fmt::print("keeping {}\n", shell::get_u8path(results.temp_dir));
			}
		}
	}

	fmt::print("\nrunning linear....\n");

	for (auto& test : tests) {
		if (!(RUN_LINEAR || test.linear)) continue;

		auto results = run_test(test, variables, rt);
		counters.report(results.result, results.task_ident,
		                results.report ? *results.report : ""sv,
		                results.prepare, rt.debug);
		if (!keep_dirs) {
			std::error_code ignore{};
			fs::remove_all(results.temp_dir, ignore);
		} else {
			fmt::print("keeping {}\n", shell::get_u8path(results.temp_dir));
		}
	}

	if (!counters.summary(tests.size())) return 1;

	return 0;
}
