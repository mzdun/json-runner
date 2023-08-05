// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <chaiscript/chaiscript.hpp>
#include "base/str.hh"

inline std::string str(std::string_view view) {
	return {view.data(), view.size()};
}

chaiscript::ModulePtr bootstrap_string(
    chaiscript::ModulePtr m = std::make_shared<chaiscript::Module>()) {
	using namespace chaiscript;

	m->add(fun([](std::string const& s) { return str(trim_left(s)); }),
	       "trim_left");
	m->add(fun([](std::string const& s) { return str(trim_right(s)); }),
	       "trim_right");
	m->add(fun([](std::string const& s) { return str(trim(s)); }), "trim");
	m->add(fun([](std::string const& s) { return str(tolower(s)); }),
	       "tolower");
	m->add(fun([](std::string const& s) { return str(toupper(s)); }),
	       "toupper");
	m->add(fun([](std::string const& s, char c) { return split_str(s, c); }),
	       "split");
	m->add(fun([](std::string const& s, std::string const& prefix) {
		       return s.starts_with(prefix);
	       }),
	       "starts_with");
	m->add(fun([](std::string const& s, std::string const& prefix) {
		       return s.ends_with(prefix);
	       }),
	       "ends_with");

	return m;
}
