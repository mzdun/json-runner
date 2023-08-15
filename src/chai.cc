// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#define NOMINMAX

#include "chai.hh"
#include <fmt/format.h>
#include <chaiscript/chaiscript.hpp>
#include <chaiscript/dispatchkit/bootstrap_stl.hpp>
#include <string>
#include "base/shell.hh"
#include "base/str.hh"
#include "bindings/filesystem.hh"
#include "bindings/runner.hh"
#include "bindings/span.hh"
#include "bindings/string.hh"
#include "io/file.hh"
#include "testbed/test.hh"

using namespace std::literals;

namespace {
	void print_filename(std::string_view filename,
	                    chaiscript::File_Position const& pos) {
		fmt::print(stderr, "{}:", filename);
		if (pos.line) {
			fmt::print(stderr, "{}:", pos.line);
			if (pos.column) {
				fmt::print(stderr, "{}:", pos.column);
			}
		}
		fmt::print(stderr, " ");
	}

	void print_exception(chaiscript::exception::eval_error const& ee) {
		if (!ee.filename.empty()) {
			print_filename(ee.filename, ee.start_position);
		} else {
			for (auto const& call : ee.call_stack) {
				if (call.filename().empty()) continue;
				print_filename(call.filename(), call.start());
				break;
			}
		}
		fmt::print(stderr, "error: {}\n", ee.reason);
		if (!ee.detail.empty()) fmt::print(stderr, "{}\n", ee.detail);
	}

	bool run_tool(fs::path const& name,
	              std::span<std::string const> args,
	              fs::path const& cwd,
	              std::string& listing) {
		io::args_storage copy{.stg{args.begin(), args.end()}};
		auto proc = io::run({.exec = name,
		                     .args = copy.args(),
		                     .cwd = &cwd,
		                     .output = io::terminal{},
		                     .error = io::redir_to_output{},
		                     .debug = &listing});

		if (!proc.output.empty()) {
			if (proc.output.back() != '\n') proc.output.push_back('\n');
			listing.append(proc.output);
		}
		return proc.return_code == 0;
	}

	void git_config(std::string&& name, std::string&& value) {
		io::args_storage stg{
		    .stg{"config"s, "--global"s, std::move(name), std::move(value)}};
		io::run({.exec = "git"sv,
		         .args = stg.args(),
		         .output = io::devnull{},
		         .error = io::devnull{}});
	}

	void config_git() {
		io::args_storage stg{.stg{"config"s, "--global"s, "user-name"s}};

		auto proc = io::run({.exec = "git"sv,
		                     .args = stg.args(),
		                     .output = io::piped{},
		                     .error = io::devnull{}});
		if (!proc.output.empty()) return;

		git_config("user.email"s, "test_runner@example.com"s);
		git_config("user.name"s, "Test Runner"s);
		git_config("init.defaultBranch"s, "main"s);
	}
}  // namespace

struct Project {
	Chai::ProjectInfo info{};

	static chaiscript::ModulePtr bootstrap() {
		auto m = std::make_shared<chaiscript::Module>();
		bootstrap(*m);
		return m;
	}

	static void bootstrap_project(chaiscript::Module& m) {
		using namespace chaiscript;

		m.add(user_type<Project>(), "Project");
		m.add(fun([](std::string const& target) -> Project {
			      return {{.target = target}};
		      }),
		      "project");
		m.add(fun([](Project& project, std::string const& prog) {
			      project.info.allowed.push_back(prog);
		      }),
		      "allow");
		m.add(fun([](Project& project, std::string const& comp) {
			      project.info.install_components.push_back(comp);
		      }),
		      "install_component");
		m.add(fun([](Project& project, std::string const& dirname) {
			      project.info.datasets_dir = dirname;
			      project.info.default_dataset = std::nullopt;
		      }),
		      "datasets");
		m.add(fun([](Project& project, std::string const& dirname,
		             std::string const& def_set) {
			      project.info.datasets_dir = dirname;
			      project.info.default_dataset = def_set;
		      }),
		      "datasets");
		m.add(fun([](Project& project, std::string const& var,
		             std::string const& value) {
			      project.info.environment[var] = value;
		      }),
		      "environment");
		m.add(fun([](Project& project, std::string const& regex,
		             std::string const& value) {
			      project.info.common_patches[regex] = value;
		      }),
		      "register_patch");

		bootstrap::standard_library::span_type<std::span<std::string const>>(
		    "StringSpan", m);
		m.add(
		    fun([](Project& project, std::string const& key, unsigned min_args,
		           std::function<bool(testbed::test&,
		                              std::span<std::string const>)> const&
		               code) {
			    auto pass_through = [code](struct testbed::commands& handler,
			                               std::span<std::string const> args,
			                               std::string&) {
				    try {
					    return code(static_cast<testbed::test&>(handler), args);
				    } catch (chaiscript::exception::eval_error const& ee) {
					    print_exception(ee);
					    std::exit(1);
				    }
			    };
			    project.info.script_handlers[key] = {min_args, pass_through};
		    }),
		    "handle");
	}

	static void bootstrap(chaiscript::Module& m) {
		chaiscript::runner::bootstrap_file(m);
		chaiscript::runner::bootstrap_runtime(m);
		chaiscript::runner::bootstrap_test(m);
		bootstrap_project(m);

		using namespace chaiscript;

		m.add(fun([](std::string const& re) -> std::string {
			      auto size = re.length();
			      for (auto c : re) {
				      if (runner::is_regex_special(c)) ++size;
			      }

			      std::string result{};
			      result.reserve(size);
			      for (auto c : re) {
				      if (runner::is_regex_special(c)) result.push_back('\\');
				      result.push_back(c);
			      }
			      return result;
		      }),
		      "re_escape");
	}
};

struct Chai::Impl {
	chaiscript::ChaiScript chai{};
	ProjectInfo project{};

	Impl() {
		try {
			chai.add(Project::bootstrap());
			chai.add(bootstrap_string());
			chai.register_namespace(
			    [&chai = chai](auto& fs) { register_fs(chai, fs); }, "fs");

			auto const path = "./runner.chai"s;
			auto value = chai.eval_file(path);
			auto state = chai.get_state();

			bool seen_project{false};
			for (auto& [name, global] : state.engine_state.m_global_objects) {
				if (!global.is_type(chaiscript::user_type<Project>())) continue;
				if (seen_project) {
					throw chaiscript::exception::eval_error(
					    fmt::format("Only one project per `runner.chai`"),
					    {1, 1}, path);
				}
				seen_project = true;
				auto named = chai.boxed_cast<Project>(global);
				project = named.info;
			}

			if (!seen_project) {
				throw chaiscript::exception::eval_error(
				    fmt::format("Project definition missing in `runner.chai`; "
				                "please call `var name = project(\"exe\");`"),
				    {1, 1}, path);
			}

			auto const install_name = project.target + "_install";
			auto const proxy = fmt::format(
			    "fun(copy_dir, rt) {{ {}(copy_dir, rt); }}", install_name);
			auto installer = chai.eval<
			    std::function<void(std::string const&, testbed::runtime&)>>(
			    proxy);
			project.installer = [installer](std::string const& copy_dir,
			                                testbed::runtime& rt) {
				try {
					installer(copy_dir, rt);
				} catch (chaiscript::exception::eval_error const& ee) {
					print_exception(ee);
					std::exit(1);
				}
			};
		} catch (chaiscript::exception::eval_error const& ee) {
			print_exception(ee);
			std::exit(1);
		} catch (std::exception const& e) {
			fmt::print(stderr, "? error: {}\n", e.what());
			std::exit(1);
		}
	}
};

Chai::Chai() = default;
Chai::~Chai() = default;
Chai::ProjectInfo const& Chai::project() noexcept {
	if (!pimpl) pimpl = std::make_unique<Chai::Impl>();
	return pimpl->project;
}

std::map<std::string, testbed::handler_info> Chai::ProjectInfo::handlers()
    const {
	auto results = testbed::commands::handlers();

	for (auto const& app : allowed) {
		results[app] = {
		    .min_args = 0,
		    .handler =
		        [app](testbed::commands& handler,
		              std::span<std::string const> args, std::string& listing) {
			        auto& self = static_cast<testbed::test&>(handler);
			        return run_tool(app, args, self.cwd(), listing);
		        },
		};

		if (app == "git"sv) {
			config_git();
		}
	}

	for (auto const& [key, handler] : script_handlers) {
		results[key] = handler;
	}

	results[target] = {
	    .min_args = 0,
	    .handler =
	        [](testbed::commands& handler, std::span<std::string const> args,
	           std::string& listing) {
		        auto& self = static_cast<testbed::test&>(handler);
		        return run_tool(self.current_rt->rt_target, args, self.cwd(),
		                        listing);
	        },
	};

	return results;
}