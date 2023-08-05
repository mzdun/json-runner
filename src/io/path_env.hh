// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace io {
#ifdef _WIN32
	static constexpr auto pathsep = L';';
#endif
#ifdef __linux__
	static constexpr auto pathsep = ':';
#endif

	template <typename Char>
	std::vector<std::basic_string_view<Char>> split(
	    std::basic_string<Char> const& initial,
	    std::basic_string<Char> const& list_string,
	    Char sep = pathsep) {
		using string_view = std::basic_string_view<Char>;
		string_view list{list_string};
		std::vector<string_view> result{};

		result.reserve([list, sep, initial] {
			size_t length = 1;
			if (!initial.empty()) ++length;
			auto pos = list.find(sep);
			while (pos != string_view::npos) {
				++length;
				pos = list.find(sep, pos + 1);
			}
			return length;
		}());

		if (!initial.empty()) result.push_back(initial);

		auto pos = list.find(sep);
		decltype(pos) prev = 0;
		while (pos != string_view::npos) {
			auto const end = pos;
			auto const start = prev;
			prev = pos + 1;
			pos = list.find(sep, prev);
			if (end == start)  // empty string
				continue;
			result.push_back(list.substr(start, end - start));
		}
		if (prev != list.length()) {
			result.push_back(list.substr(prev));
		}

		return result;
	}  // GCOV_EXCL_LINE[GCC]
	template <typename Char>
	std::vector<std::basic_string_view<Char>> split(
	    std::basic_string<Char>&& list,
	    Char sep = pathsep) = delete;
}  // namespace io
