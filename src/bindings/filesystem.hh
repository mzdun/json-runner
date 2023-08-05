// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <chaiscript/chaiscript.hpp>
#include "base/shell.hh"

namespace chaiscript::bootstrap::standard_library {
	template <typename Container>
	auto adl_begin(Container& cont) {
		using std::begin;
		return begin(cont);
	}

	template <typename Container>
	auto adl_end(Container& cont) {
		using std::end;
		return end(cont);
	}

	template <typename Container, typename IterType>
	struct Input_Range {
		typedef Container container_type;

		Input_Range(Container& c) : m_begin(adl_begin(c)), m_end(adl_end(c)) {}

		bool empty() const { return m_begin == m_end; }

		void pop_front() {
			if (empty()) {
				throw std::range_error("Range empty");
			}
			++m_begin;
		}

		decltype(auto) front() const {
			if (empty()) {
				throw std::range_error("Range empty");
			}
			return (*m_begin);
		}

		IterType m_begin;
		IterType m_end;
	};

	namespace detail {
		template <typename Bidir_Type>
		void input_range_type_impl_2(const std::string& type, Module& m) {
			m.add(user_type<Bidir_Type>(), type + "_Range");

			copy_constructor<Bidir_Type>(type + "_Range", m);

			m.add(
			    constructor<Bidir_Type(typename Bidir_Type::container_type&)>(),
			    "range_internal");

			m.add(fun(&Bidir_Type::empty), "empty");
			m.add(fun(&Bidir_Type::pop_front), "pop_front");
			m.add(fun(&Bidir_Type::front), "front");
		}
	}  // namespace detail

	template <typename IterType>
	void directory_iterator_type(const std::string& type, Module& m) {
		m.add(user_type<IterType>(), type);

		default_constructible_type<IterType>(type, m);
		assignable_type<IterType>(type, m);

		detail::input_range_type_impl_2<Input_Range<IterType, IterType>>(type,
		                                                                 m);
	}
	template <typename IterType>
	ModulePtr directory_iterator_type(const std::string& type) {
		auto m = std::make_shared<Module>();
		directory_iterator_type<IterType>(type, *m);
		return m;
	}
}  // namespace chaiscript::bootstrap::standard_library

void register_fs(chaiscript::ChaiScript& chai, chaiscript::Namespace& fs) {
	using namespace chaiscript;

	chai.add(chaiscript::bootstrap::standard_library::directory_iterator_type<
	         fs::directory_iterator>("directory_iterator_type"));

	chai.add(user_type<fs::directory_entry>(), "directory_entry_type");
	chai.add(fun([](fs::directory_entry const& entry) {
		         return shell::get_path(entry.path());
	         }),
	         "path");

	fs["parent_path"] = var(fun([](std::string const& path) {
		return shell::get_path(shell::make_u8path(path).parent_path());
	}));
	fs["filename"] = var(fun([](std::string const& path) {
		return shell::get_path(shell::make_u8path(path).filename());
	}));
	fs["stem"] = var(fun([](std::string const& path) {
		return shell::get_path(shell::make_u8path(path).stem());
	}));
	fs["extension"] = var(fun([](std::string const& path) {
		return shell::get_path(shell::make_u8path(path).extension());
	}));

	fs["join"] = var(fun([](std::string const& p1, std::string const& p2) {
		return shell::get_path(shell::make_u8path(p1) / shell::make_u8path(p2));
	}));
	fs["abspath"] = var(fun([](std::string const& path) {
		return shell::get_u8path(fs::absolute(shell::make_u8path(path)));
	}));
	fs["create_directories"] = var(fun([](std::string const& path) {
		fs::create_directories(shell::make_u8path(path));
	}));
	fs["copy"] = var(fun([](std::string const& src, std::string const& dst) {
		fs::copy(shell::make_u8path(src), shell::make_u8path(dst));
	}));
	fs["directory_iterator"] = var(fun([](std::string const& path) {
		return fs::directory_iterator{shell::make_u8path(path)};
	}));
}
