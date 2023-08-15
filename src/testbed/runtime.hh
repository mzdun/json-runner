// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <set>
#include "testbed/commands.hh"

namespace testbed {
	enum class exp { generic, preferred, not_changed };

	struct runtime {
		fs::path target;
		fs::path rt_target{target};
		fs::path build_dir;
		fs::path temp_dir;
		std::string version;
		size_t counter_total{};
		size_t counter_digits{counter_width(counter_total)};
		std::map<std::string, handler_info> handlers;
		std::set<std::string> reportable_vars{};
		std::map<std::string, std::string>* variables;
		std::map<std::string, std::string> const* chai_variables;
		std::map<std::string, std::string> const* common_patches;
		bool debug{true};

		fs::path mocks_dir() const { return temp_dir / "mocks"sv; }

		std::string expand(std::string const& arg,
		                   std::map<std::string, std::string> const& stored_env,
		                   exp modifier) const;
		io::args_storage expand(
		    std::span<std::string const> cmd,
		    std::map<std::string, std::string> const& stored_env,
		    exp modifier) const;
		bool run(commands& handler,
		         std::span<std::string const> args,
		         std::string& listing) const;
		void fix(std::string& text,
		         std::vector<std::pair<std::string, std::string>> const&
		             patches) const;

	private:
		static size_t counter_width(size_t total) noexcept {
			size_t digits = 1;
			while (total > 9) {
				digits += 1;
				total /= 10;
			}
			return digits;
		}
	};
}  // namespace testbed
