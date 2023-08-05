// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <optional>
#include <unordered_set>
#include "testbed/test.hh"

namespace io::cmake {
	struct preset {
		std::optional<fs::path> binary_dir{};
		std::vector<std::string> inherits{};
		std::string CMAKE_BUILD_TYPE{};

		static std::map<std::string, preset> load_file(
		    fs::path const& filename);

		std::optional<fs::path> get_binary_dir(
		    std::map<std::string, preset> const& presets) const;

		std::optional<std::string> get_build_type(
		    std::map<std::string, preset> const& presets) const;

	private:
		std::optional<fs::path> get_binary_dir(
		    std::map<std::string, preset> const& presets,
		    std::unordered_set<std::string>& seen) const;
		std::optional<std::string> get_build_type(
		    std::map<std::string, preset> const& presets,
		    std::unordered_set<std::string>& seen) const;
		static preset from(json::map& data, fs::path const& source_root);
		static void load_file(fs::path const& filename,
		                      fs::path const& source_root,
		                      std::map<std::string, preset>& out,
		                      std::unordered_set<fs::path>& seen);
	};
}  // namespace io::cmake
