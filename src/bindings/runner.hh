// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <chaiscript/chaiscript.hpp>
#include "base/shell.hh"
#include "io/file.hh"
#include "testbed/test.hh"

namespace chaiscript::runner {
	static void bootstrap_file(chaiscript::Module& m) {
		using namespace chaiscript;

		m.add(user_type<io::file>(), "file_type");
		m.add(fun([](std::string const& filename) {
			      return io::fopen(filename);
		      }),
		      "open");
		m.add(fun([](std::string const& filename, std::string const& mode) {
			      return io::fopen(filename, mode.empty() ? "r" : mode.c_str());
		      }),
		      "open");

		m.add(fun([](io::file& f) { f.close(); }), "close");
		m.add(fun([](io::file& f) { return !f; }), "!");
		m.add(fun([](io::file const& f) -> std::string {
			      auto bytes = f.read();
			      return {reinterpret_cast<char const*>(bytes.data()),
			              bytes.size()};
		      }),
		      "read");
		m.add(fun([](io::file const& f, std::string const& contents) {
			      f.store(contents.data(), contents.size());
		      }),
		      "write");
	}

	static void bootstrap_runtime(chaiscript::Module& m) {
		using namespace chaiscript;

		m.add(user_type<testbed::runtime>(), "runtime");
		m.add(fun([](testbed::runtime& rt, std::string const& name,
		             std::string const& path) -> void {
			      auto& variables = *rt.variables;
			      shell::append(variables, name, path);
			      shell::putenv(name, variables[name]);
			      rt.reportable_vars.insert(name);
		      }),
		      "append");
		m.add(fun([](testbed::runtime& rt, std::string const& name,
		             std::string const& path) -> void {
			      auto& variables = *rt.variables;
			      shell::prepend(variables, name, path);
			      shell::putenv(name, variables[name]);
			      rt.reportable_vars.insert(name);
		      }),
		      "prepend");
#define RT_PATH(NAME)                          \
	m.add(fun([](testbed::runtime const& rt) { \
		      return shell::get_path(rt.NAME); \
	      }),                                  \
	      #NAME)
		RT_PATH(target);
		RT_PATH(rt_target);
		RT_PATH(build_dir);
		RT_PATH(temp_dir);
	}
#undef RT_PATH

	static void bootstrap_test(chaiscript::Module& m) {
		using namespace chaiscript;

		m.add(user_type<testbed::test>(), "test");
		m.add(fun([](testbed::test const& test, std::string const& path) {
			      return shell::get_path(test.path(path));
		      }),
		      "path");
	}

	static bool is_regex_special(char c) {
		switch (c) {
			case '.':
			case '+':
			case '*':
			case '?':
			case '^':
			case '$':
			case '(':
			case ')':
			case '[':
			case ']':
			case '{':
			case '}':
			case '|':
			case '\\':
				return true;
		}
		return false;
	}
}  // namespace chaiscript::runner