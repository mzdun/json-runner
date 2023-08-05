// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "base/str.hh"
#include <fmt/format.h>
#include "base/seed_sequence.hh"

using namespace std::literals;

namespace {
	class letters {
	public:
		std::string random(size_t size) {
			static constexpr auto ascii_letters =
			    "abcdefghijklmnopqrstuvwxyz"
			    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			    ""sv;
			std::string result{};
			result.reserve(size);
			std::uniform_int_distribution<size_t> dice{
			    0, sizeof(ascii_letters) - 1};
			for (size_t count = 0; count < size; ++count) {
				auto index = dice(rand);
				result.push_back(ascii_letters[index]);
			}
			return result;
		}

	private:
		std::mt19937 rand = seed_sequence::mt19937();
	};
}  // namespace

std::string last_enter(std::string_view text) {
	if (!text.empty() && text.back() == '\n') {
		return fmt::format("{}\\n", text.substr(0, text.size() - 1));
	}
	return {text.data(), text.size()};
}

std::string repr(std::string_view str) {
	auto len = str.size() + 2;
	for (auto c : str) {
		switch (c) {
			case '"':
			case '\\':
			case '\a':
			case '\b':
			case '\f':
			case '\n':
			case '\r':
			case '\t':
			case '\v':
				++len;
				break;
			default:
				if (!std::isprint(static_cast<unsigned char>(c))) len += 3;
		}
	}
	std::string result{};
	result.push_back('"');
	for (auto c : str) {
		switch (c) {
			case '"':
			case '\\':
				result.push_back('\\');
				result.push_back(c);
				break;
			case '\a':
				result += "\\a"sv;
				break;
			case '\b':
				result += "\\b"sv;
				break;
			case '\f':
				result += "\\f"sv;
				break;
			case '\n':
				result += "\\n"sv;
				break;
			case '\r':
				result += "\\r"sv;
				break;
			case '\t':
				result += "\\t"sv;
				break;
			case '\v':
				result += "\\v"sv;
				break;
			default:
				if (std::isprint(static_cast<unsigned char>(c)))
					result.push_back(c);
				else
					result.append(
					    fmt::format("\\x{:2X}", static_cast<unsigned char>(c)));
		}
	}
	result.push_back('"');
	return result;
}

std::string random_letters(size_t size) { return letters{}.random(size); }
