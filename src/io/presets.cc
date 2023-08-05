// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "io/presets.hh"
#include "base/str.hh"
#include "io/file.hh"

namespace io::cmake {
	std::map<std::string, preset> preset::load_file(fs::path const& filename) {
		std::map<std::string, preset> out{};
		std::unordered_set<fs::path> seen{};
		auto const canon = fs::weakly_canonical(filename);
		load_file(canon, canon.parent_path(), out, seen);
		return out;
	}

	std::optional<fs::path> preset::get_binary_dir(
	    std::map<std::string, preset> const& presets) const {
		std::unordered_set<std::string> seen{};
		return get_binary_dir(presets, seen);
	};

	std::optional<fs::path> preset::get_binary_dir(
	    std::map<std::string, preset> const& presets,
	    std::unordered_set<std::string>& seen) const {
		if (binary_dir) return binary_dir;
		for (auto const& inherit : inherits) {
			auto it = presets.find(inherit);
			if (it != presets.end()) {
				auto cand = it->second.get_binary_dir(presets, seen);
				if (cand) return cand;
			}
		}
		return std::nullopt;
	};

	std::optional<std::string> preset::get_build_type(
	    std::map<std::string, preset> const& presets) const {
		std::unordered_set<std::string> seen{};
		return get_build_type(presets, seen);
	}

	std::optional<std::string> preset::get_build_type(
	    std::map<std::string, preset> const& presets,
	    std::unordered_set<std::string>& seen) const {
		if (!CMAKE_BUILD_TYPE.empty()) return CMAKE_BUILD_TYPE;
		for (auto const& inherit : inherits) {
			auto it = presets.find(inherit);
			if (it != presets.end()) {
				auto cand = it->second.get_build_type(presets, seen);
				if (cand) return cand;
			}
		}
		return std::nullopt;
	}

	preset preset::from(json::map& data, fs::path const& source_root) {
		preset out{};

		if (auto binary_dir = cast<json::string>(data, u8"binaryDir");
		    binary_dir) {
			static constexpr auto source_dir = u8"${sourceDir}/"sv;
			if (binary_dir->starts_with(source_dir)) {
				out.binary_dir =
				    source_root / binary_dir->substr(source_dir.length());
			} else {
				out.binary_dir = *binary_dir;
			}
			out.binary_dir->make_preferred();
		}

		if (auto inherits = cast<json::array>(data, u8"inherits"); inherits) {
			out.inherits.reserve(inherits->size());
			for (auto const& node : *inherits) {
				auto inherit = cast<json::string>(node);
				if (inherit) out.inherits.push_back(from_u8s(*inherit));
			}
		}

		if (auto cache = cast<json::map>(data, u8"cacheVariables"); cache) {
			if (auto build_type =
			        cast<json::string>(cache, u8"CMAKE_BUILD_TYPE");
			    build_type) {
				out.CMAKE_BUILD_TYPE = from_u8s(*build_type);
			}
		}

		return out;
	}

	void preset::load_file(fs::path const& filename,
	                       fs::path const& source_root,
	                       std::map<std::string, preset>& out,
	                       std::unordered_set<fs::path>& seen) {
		auto const canon = fs::weakly_canonical(filename);
		auto [_, freshly_added] = seen.insert(canon);
		if (!freshly_added) return;

		auto file = io::fopen(canon);
		if (!file) return;
		auto data = file.read();
		auto root = json::read_json(
		    {reinterpret_cast<char8_t const*>(data.data()), data.size()});

		if (auto include = cast<json::array>(root, u8"include"); include) {
			auto dirname = canon.parent_path();
			for (auto const& node : *include) {
				auto path = cast<json::string>(node);
				if (path) load_file(dirname / *path, source_root, out, seen);
			}
		}

		if (auto presets = cast<json::array>(root, u8"configurePresets");
		    presets) {
			for (auto& node : *presets) {
				auto map = cast<json::map>(node);
				if (!map) continue;
				auto name = cast<json::string>(map, u8"name");
				if (!name) continue;
				out[from_u8s(*name)] = preset::from(*map, source_root);
			}
		}
	}
}  // namespace io::cmake
