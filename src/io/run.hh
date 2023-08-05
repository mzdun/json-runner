// Copyright (c) 2022 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <args/parser.hpp>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace io {
	struct capture {
		int return_code{};
		std::string output{};
		std::string error{};

		bool operator==(capture const&) const noexcept = default;
	};

	enum class pipe {
		none = 0x0000,
		output = 0x0001,
		error = 0x0002,
		input = 0x0004,
		output_devnull = 0x0008,
		error_devnull = 0x0010,
		input_devnull = 0x0020,
		outs = output | error,
		all = input | output | error,
	};

	inline pipe operator&(pipe lhs, pipe rhs) noexcept {
		return static_cast<pipe>(std::to_underlying(lhs) &
		                         std::to_underlying(rhs));
	}

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

	struct run_opts {
		fs::path const& exec;
		args::arglist args{};
		fs::path const* cwd{nullptr};
		std::map<std::string, std::string> const* env{nullptr};
		pipe pipe{pipe::none};
		std::string_view input{};
	};
	capture run(run_opts const& options);

	struct call_opts {
		fs::path const& exec;
		args::arglist args{};
		fs::path const* cwd{nullptr};
		std::map<std::string, std::string> const* env{nullptr};
	};
	inline int call(call_opts const& options) {
		return run({.exec = options.exec,
		            .args = options.args,
		            .cwd = options.cwd,
		            .env = options.env,
		            .pipe = pipe::none})
		    .return_code;
	}

	std::optional<fs::path> find_program(std::span<std::string const> names,
	                                     fs::path const& hint);
}  // namespace io
