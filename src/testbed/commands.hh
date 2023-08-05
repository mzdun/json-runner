// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <args/parser.hpp>
#include <array>
#include <filesystem>
#include <functional>
#include <json/json.hpp>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "io/run.hh"

namespace fs = std::filesystem;

namespace testbed {
	using strlist = std::vector<std::string>;

	struct handler_info {
		unsigned min_args{1};
		std::function<bool(struct commands&, std::span<std::string const>)>
		    handler;
	};

	struct commands {
		virtual ~commands();

		fs::path path(fs::path const& p) const { return cwd_ / p; }
		fs::path const& cwd() const noexcept { return cwd_; }

		// commands:
		virtual bool cd(fs::path const& dir);
		bool make_ro(fs::path const& path) const;
		bool cp(fs::path const& src, fs::path const& dst) const;
		bool mkdirs(fs::path const& path) const;
		bool rmtree(fs::path const& path) const;
		bool touch(fs::path const& filename, std::string const* content) const;
		bool unpack(fs::path const& archive, fs::path const& dst) const;
		virtual bool store_variable(std::string const& name,
		                            std::span<std::string const> call) = 0;
		virtual bool mock(std::string const& exe, std::string const& link) = 0;
		virtual bool generate(std::string const& tmplt,
		                      std::string const& dst,
		                      std::span<std::string const> args) = 0;
		bool shell() const;

		static std::map<std::string, handler_info> handlers();

	private:
		fs::path cwd_{fs::current_path()};
	};
}  // namespace testbed
