// Copyright (c) 2022 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <args/parser.hpp>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace io {
	struct capture {
		int return_code{};
		std::string output{};
		std::string error{};

		bool operator==(capture const&) const noexcept = default;
	};

	struct args_storage {
		std::vector<std::string> stg{};
		std::vector<char*> link_stg{};

		::args::arglist args() {
			link_stg.resize(stg.size());
			auto dst = link_stg.begin();
			for (auto& arg : stg)
				*dst++ = arg.data();
			return {static_cast<int>(link_stg.size()), link_stg.data()};
		}
	};

#define TAG_STRUCT(NAME)                                             \
	struct NAME {                                                    \
		bool operator==(NAME const&) const noexcept { return true; } \
	}

	TAG_STRUCT(piped);
	TAG_STRUCT(devnull);
	TAG_STRUCT(redir_to_output);
	TAG_STRUCT(redir_to_error);
	TAG_STRUCT(terminal);

	struct stream_decl : std::variant<std::nullptr_t,
	                                  piped,
	                                  devnull,
	                                  redir_to_output,
	                                  redir_to_error,
	                                  terminal> {
		using base = std::variant<std::nullptr_t,
		                          piped,
		                          devnull,
		                          redir_to_output,
		                          redir_to_error,
		                          terminal>;
		using base::base;

		template <typename Tag>
		    requires std::same_as<Tag, std::nullptr_t> ||
		             std::same_as<Tag, piped> || std::same_as<Tag, devnull> ||
		             std::same_as<Tag, redir_to_output> ||
		             std::same_as<Tag, redir_to_error> ||
		             std::same_as<Tag, terminal>
		friend bool operator==(stream_decl const& decl, Tag rhs) noexcept {
			return std::holds_alternative<Tag>(decl);
		}

		friend bool operator==(stream_decl const& decl,
		                       base const& rhs) noexcept {
			return static_cast<base const&>(decl) == rhs;
		}
	};

	struct run_opts {
		fs::path const& exec;
		args::arglist args{};
		fs::path const* cwd{nullptr};
		std::map<std::string, std::string> const* env{nullptr};
		std::optional<std::string_view> input{};
		stream_decl output{};
		stream_decl error{};
		std::string* debug{nullptr};
	};
	capture run(run_opts const& options);

	struct call_opts {
		fs::path const& exec;
		args::arglist args{};
		fs::path const* cwd{nullptr};
		std::map<std::string, std::string> const* env{nullptr};
		std::string* debug{nullptr};
	};
	inline int call(call_opts const& options) {
		return run({.exec = options.exec,
		            .args = options.args,
		            .cwd = options.cwd,
		            .env = options.env,
		            .output = piped{},
		            .debug = options.debug})
		    .return_code;
	}  // namespace io

	std::optional<fs::path> find_program(std::span<std::string const> names,
	                                     fs::path const& hint);
}  // namespace io
