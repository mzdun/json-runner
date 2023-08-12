// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <array>
#include <filesystem>
#include <json/json.hpp>
#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>
#include "io/run.hh"
#include "testbed/runtime.hh"

namespace fs = std::filesystem;

namespace testbed {
	enum class check { begin, end, all };
	using checks = std::array<check, 2>;

	using test_variable =
	    std::variant<std::nullptr_t, std::string, std::vector<std::string>>;

	struct test_data {
		fs::path filename{};
		size_t index{};
		json::map data{};
		std::string lang{};
		std::vector<strlist> prepare{};
		strlist call_args{};
		std::vector<strlist> post{};
		std::vector<strlist> cleanup{};
		std::optional<io::capture> expected{};
		std::string name = test_name();
		bool linear{true};
		std::variant<bool, std::string> disabled{false};
		bool ok{not_disabled()};
		bool needs_mocks_in_path{false};
		runtime const* current_rt{nullptr};
		std::map<std::string, std::string> stored_env{};
		std::map<std::string, test_variable> env{};
		std::vector<std::pair<std::string, std::string>> patches{};
		checks check{testbed::check::all, testbed::check::all};
		struct out_capture_t {
			io::stream_decl output{io::piped{}};
			io::stream_decl error{io::piped{}};
		} out_capture{};

		static test_data load(fs::path const& filename,
		                      size_t index,
		                      std::optional<std::string> const& schema,
		                      bool& renovate);

	private:
		static inline std::string name_for(std::string_view name);
		std::string test_name() const;
		bool not_disabled() const;
	};

	struct test_run_results {
		std::string prepare{};
		std::optional<io::capture> capture{};
	};
	struct test : test_data, commands {
		static constexpr size_t HORIZ_SPACE = 20;
		test(test_data&& data) : test_data{std::move(data)} {}

		bool cd(fs::path const& dir) override;
		bool store_variable(std::string const& name,
		                    std::span<std::string const> call,
		                    std::string& debug) override;
		bool mock(std::string const& exe, std::string const& link) override;
		bool generate(std::string const& tmplt,
		              std::string const& dst,
		              std::span<std::string const> args,
		              std::string& listing) override;

		static test load(fs::path const& filename,
		                 size_t index,
		                 std::optional<std::string> const& schema) {
			bool renovate{false};
			auto result =
			    test{test_data::load(filename, index, schema, renovate)};
			if (result.ok && renovate) {
				result.store();
			}
			return result;
		}

		bool run_cmds(runtime const&,
		              std::span<strlist const> commands,
		              std::string& listing);

		test_run_results run(std::map<std::string, std::string> const&,
		                     runtime const&);
		io::capture clip(io::capture const&) const;
		std::string report(io::capture const&, runtime const&) const;

		void nullify(std::optional<std::string> const& lang);
		void store() const;

	private:
		std::pair<io::args_storage, std::vector<io::args_storage>>
		expand_test_calls(runtime const& environment) const;
		std::map<std::string, std::string> copy_environment_block(
		    std::map<std::string, std::string> const& variables,
		    runtime const& environment) const;
		io::capture observe(
		    std::pair<io::args_storage, std::vector<io::args_storage>>& calls,
		    std::map<std::string, std::string> const& variables,
		    runtime const& environment,
		    std::string& listing) const;
	};
}  // namespace testbed
