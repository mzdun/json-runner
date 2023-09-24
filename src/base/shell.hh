// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <concepts>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "base/str.hh"

namespace fs = std::filesystem;

namespace shell {
	std::string quote(std::string_view arg);

	template <typename T>
	concept StringLike =
	    (std::same_as<T, std::string> || std::same_as<T, std::string_view>);

	template <StringLike Arg>
	std::string join(std::span<Arg const> args) {
		std::string result{};
		for (auto const& arg : args) {
			if (!result.empty()) result.push_back(' ');
			result.append(quote(arg));
		}
		return result;
	}

	template <StringLike Arg>
	std::string join(std::vector<Arg> const& args) {
		return join(std::span{args});
	}

	std::vector<std::string> split(std::string_view line);

#ifdef _WIN32
	static constexpr auto pathsep = ';';
#endif
#ifdef __linux__
	static constexpr auto pathsep = ':';
#endif

	std::map<std::string, std::string> get_env();
	void append(std::map<std::string, std::string>& env,
	            std::string const& var,
	            fs::path const& dir);
	void prepend(std::map<std::string, std::string>& env,
	             std::string const& var,
	             fs::path const& dir);
	void putenv(std::string const& name, std::string const& var);
	std::string getenv(std::string const& name);

	inline fs::path make_u8path(std::string_view u8) {
		return std::u8string_view{reinterpret_cast<char8_t const*>(u8.data()),
		                          u8.size()};
	}

	inline std::string get_u8path(fs::path copy) {
		copy.make_preferred();
		return from_u8s(copy.u8string());
	}

	inline std::string get_generic_path(fs::path const& path) {
#ifdef _WIN32
		auto copy = path.native();
		for (auto& c : copy) {
			if (c == L'\\') c = L'/';
		}
		return from_u8s(fs::path{copy}.u8string());
#else
		return from_u8s(path.u8string());
#endif
	}

	inline std::string get_path(fs::path const& path) {
		return from_u8s(path.u8string());
	}
}  // namespace shell
