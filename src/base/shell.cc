// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#define NOMINMAX

#include "base/shell.hh"
#include <ctre.hpp>

#ifdef _WIN32
#include "Windows.h"
#endif

#ifdef __linux__
#include <stdlib.h>

extern char** environ;
#endif

using namespace std::literals;

namespace shell {
	namespace {
		constexpr auto contains_unsafe = ctre::search<R"([^\w@%+=:,.\/\-])">;

		void skip_ws(std::string_view::iterator& it,
		             std::string_view::iterator const& end) {
			while (it != end && std::isspace(static_cast<unsigned char>(*it)))
				++it;
		}

		std::string quoted(std::string_view::iterator& it,
		                   std::string_view::iterator const& end,
		                   char quote) {
			std::string result{};
			auto start = it;
			while (it != end && *it != quote)
				++it;

			result.insert(result.end(), start, it);

			if (it != end) ++it;
			return result;
		}

		std::string escaped(std::string_view::iterator& it,
		                    std::string_view::iterator const& end) {
			std::string result{};
			auto escaped{false};
			while (it != end) {
				auto const c = *it;
				if (escaped) {
					result.push_back(c);
					++it;
					escaped = false;
					continue;
				}

				if (c == '\\') {
					escaped = true;
					++it;
					continue;
				}

				if (c == '\'' || c == '"' ||
				    std::isspace(static_cast<unsigned char>(c)))
					break;

				result.push_back(c);
				++it;
			}

			return result;
		}

		std::string get_argument(std::string_view::iterator& it,
		                         std::string_view::iterator const& end) {
			std::string result;
			while (it != end &&
			       !std::isspace(static_cast<unsigned char>(*it))) {
				if (*it == '\'') {
					++it;
					result.append(quoted(it, end, '\''));
					continue;
				}
				if (*it == '"') {
					++it;
					result.append(quoted(it, end, '"'));
					continue;
				}
				result.append(escaped(it, end));
			}
			skip_ws(it, end);
			return result;
		}

#ifdef _WIN32
		std::string to_utf8(std::wstring_view arg) {
			if (arg.empty()) return {};

			auto const length = static_cast<int>(arg.size());

			auto size = WideCharToMultiByte(CP_UTF8, 0, arg.data(), length,
			                                nullptr, 0, nullptr, nullptr);
			std::unique_ptr<char[]> out{new char[size + 1]};
			WideCharToMultiByte(CP_UTF8, 0, arg.data(), length, out.get(),
			                    size + 1, nullptr, nullptr);
			out[size] = 0;
			return {out.get(), static_cast<size_t>(size)};
		}

		std::wstring from_utf8(std::string_view arg) {
			if (arg.empty()) return {};

			auto const length = static_cast<int>(arg.size());

			auto size =
			    MultiByteToWideChar(CP_UTF8, 0, arg.data(), length, nullptr, 0);
			std::unique_ptr<wchar_t[]> out{new wchar_t[size + 1]};
			MultiByteToWideChar(CP_UTF8, 0, arg.data(), length, out.get(),
			                    size + 1);
			out[size] = 0;
			return {out.get(), static_cast<size_t>(size)};
		}

		std::string& toupper(std::string& s) {
			for (auto& c : s) {
				c = static_cast<char>(
				    std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}

		std::string toupper(std::string&& s) {
			for (auto& c : s) {
				c = static_cast<char>(
				    std::toupper(static_cast<unsigned char>(c)));
			}
			return s;
		}
#endif
	}  // namespace

	std::string quote(std::string_view arg) {
		if (arg.empty()) return "''"s;
		if (!contains_unsafe(arg)) return {arg.data(), arg.size()};

		auto size = arg.size() + 2;
		for (auto c : arg) {
			if (c == '\'') size += 5;
		}

		std::string result;
		result.reserve(size);
		result.push_back('\'');
		for (auto c : arg) {
			if (c == '\'')
				result.append("'\"'\"'"sv);
			else
				result.push_back(c);
		}
		result.push_back('\'');

		return result;
	}

	std::vector<std::string> split(std::string_view line) {
		std::vector<std::string> result;
		auto it = line.begin();
		auto end = line.end();
		skip_ws(it, end);
		while (it != end) {
			result.push_back(get_argument(it, end));
		}
		return result;
	}

#ifdef _WIN32
	std::map<std::string, std::string> get_env() {
		std::map<std::string, std::string> result;
		auto multi = GetEnvironmentStringsW();
		if (!multi) return result;
		while (*multi) {
			auto size = wcslen(multi);
			auto view = std::wstring_view{multi, size};
			multi += size + 1;

			auto const enter = view.find('=');
			// "=DRIVE:=DRIVE:\current\path
			if (enter == 0) continue;

			if (enter == std::wstring_view::npos) {
				result[toupper(to_utf8(view))];
			} else {
				result[toupper(to_utf8(view.substr(0, enter)))] =
				    to_utf8(view.substr(enter + 1));
			}
		}
		return result;
	}
#endif

#ifdef __linux__
	std::map<std::string, std::string> get_env() {
		std::map<std::string, std::string> result;

		for (char** en = environ; *en; en++) {
			auto view = std::string_view{*en};

			auto const enter = view.find('=');
			if (enter == std::wstring_view::npos) {
				result[toupper(view)];
			} else {
				result[toupper(view.substr(0, enter))] = view.substr(enter + 1);
			}
		}
		return result;
	}
#endif

	void append(std::map<std::string, std::string>& env,
	            std::string const& var,
	            std::filesystem::path const& dir) {
		auto it = env.lower_bound(var);
		if (it == env.end() || it->first != var) {
			env.insert(it, {var, get_u8path(dir)});
		} else {
			it->second.push_back(pathsep);
			it->second.append(get_u8path(dir));
		}
	}

	void prepend(std::map<std::string, std::string>& env,
	             std::string const& var,
	             std::filesystem::path const& dir) {
		auto it = env.lower_bound(var);
		if (it == env.end() || it->first != var) {
			env.insert(it, {var, get_u8path(dir)});
		} else {
			auto const prefix = get_u8path(dir);
			std::string new_value{};
			new_value.reserve(prefix.size() + it->second.size() + 1);
			new_value.append(prefix);
			new_value.push_back(pathsep);
			new_value.append(it->second);
			it->second = std::move(new_value);
		}
	}

#ifdef _WIN32
	void putenv(std::string const& name, std::string const& var) {
		SetEnvironmentVariableW(from_utf8(name).c_str(),
		                        from_utf8(var).c_str());
	}
#endif

#ifdef __linux__
	void putenv(std::string const& name, std::string const& var) {
		setenv(name.c_str(), var.c_str(), 1);
	}
#endif
}  // namespace shell
