// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

inline std::u8string_view to_u8(std::string_view str) {
	return {reinterpret_cast<char8_t const*>(str.data()), str.size()};
}

inline std::u8string to_u8s(std::string_view str) {
	return {reinterpret_cast<char8_t const*>(str.data()), str.size()};
}

inline std::string_view from_u8(std::u8string_view str) {
	return {reinterpret_cast<char const*>(str.data()), str.size()};
}

inline std::string from_u8s(std::u8string_view str) {
	return {reinterpret_cast<char const*>(str.data()), str.size()};
}

inline std::string_view trim_left(std::string_view s) {
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
		s = s.substr(1);
	return s;
}

inline std::string_view trim_right(std::string_view s) {
	while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
		s = s.substr(0, s.size() - 1);
	return s;
}

inline std::string_view trim(std::string_view s) {
	return trim_right(trim_left(s));
}

inline std::string tolower(std::string_view s) {
	std::string result{};
	result.assign(s);
	for (auto& c : result) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return result;
}

inline std::string toupper(std::string_view s) {
	std::string result{};
	result.assign(s);
	for (auto& c : result) {
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}
	return result;
}

template <typename Callback>
inline void split(std::string_view text, char sep, Callback&& cb) {
	auto pos = text.find(sep);
	decltype(pos) prev = 0;

	unsigned block = 0;
	while (pos != std::string_view::npos) {
		auto const block_end = pos;
		auto const block_start = prev;
		++block;
		prev = pos + 1;
		pos = text.find(sep, prev);
		cb(block, text.substr(block_start, block_end - block_start));
	}
	++block;
	cb(block, text.substr(prev));
}

inline std::vector<std::string_view> split(std::string_view text, char sep) {
	size_t length{};
	split(text, sep, [&length](auto, auto const&) { ++length; });
	std::vector<std::string_view> result{};
	result.reserve(length);
	split(text, sep, [&result](auto, auto view) { result.push_back(view); });
	return result;
}

inline std::vector<std::string> split_str(std::string_view text, char sep) {
	size_t length{};
	split(text, sep, [&length](auto, auto const&) { ++length; });
	std::vector<std::string> result{};
	result.reserve(length);
	split(text, sep, [&result](auto, auto view) {
		result.push_back({view.data(), view.size()});
	});
	return result;
}

std::string last_enter(std::string_view text);
std::string repr(std::string_view str);
std::string random_letters(size_t size);
