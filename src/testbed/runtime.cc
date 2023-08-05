// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "testbed/runtime.hh"
#include <fmt/format.h>
#include <regex>
#include "base/shell.hh"
#include "base/str.hh"

using namespace std::literals;

namespace testbed {
	namespace {
		std::string get_path(fs::path const& path, exp modifier) {
			switch (modifier) {
				case exp::generic:
					return shell::get_generic_path(path);
				case exp::preferred:
					return shell::get_u8path(path);
				case exp::not_changed:
					break;
			}
			return shell::get_path(path);
		}
	}  // namespace

	std::string runtime::expand(
	    std::string const& arg,
	    std::map<std::string, std::string> const& stored_env,
	    exp modifier) const {
		std::string result{};

		auto view = std::string_view{arg};
		auto it = view.begin();
		auto end = view.end();

		auto start = it;
		while (it != end) {
			while (it != end && *it != '$')
				++it;
			if (start != it) {
				result.append(start, it);
			}
			if (it != end) ++it;

			start = it;
			while (it != end && std::isalnum(static_cast<unsigned char>(*it)))
				++it;

			if (start != it) {
				std::string key{start, it};
				start = it;

				if (key == "TMP"sv) {
					result.append(get_path(temp_dir, modifier));
				} else if (key == "INST"sv) {
					result.append(get_path(rt_target.parent_path(), modifier));
				} else if (key == "VERSION"sv) {
					result.append(version);
				} else {
					auto replaced = false;
					for (auto const& [var, value] : *chai_variables) {
						if (key != var) continue;
						result.append(value);
						replaced = true;
						break;
					}
					if (!replaced) {
						for (auto const& [var, value] : stored_env) {
							if (key != var) continue;
							result.append(value);
							replaced = true;
							break;
						}
					}
					if (!replaced) {
						result.push_back('$');
						result.append(key);
					}
				}
			}
		}

		return result;
	}

	io::args_storage runtime::expand(
	    std::span<std::string const> cmd,
	    std::map<std::string, std::string> const& stored_env,
	    exp modifier) const {
		io::args_storage result{};
		result.stg.reserve(cmd.size());
		for (auto const& arg : cmd)
			result.stg.push_back(expand(arg, stored_env, modifier));
		return result;
	}

	bool runtime::run(commands& handler,
	                  std::span<std::string const> args) const {
		if (args.empty()) {
			fmt::print(stderr, "\033[1;31merror: command not provided\033[m\n");
			return false;
		}

		auto const& orig = args.front();
		std::string_view command = orig;
		static constexpr auto safe_ = "safe-"sv;
		auto can_fail = command.starts_with(safe_);

		if (can_fail) command = command.substr(safe_.length());

		auto it =
		    handlers.find(can_fail ? std::string{command.data(), command.size()}
		                           : args.front());
		if (debug) {
			fmt::print(stderr, "\033[1;36m> {}\033[m\n", shell::join(args));
		}

		if (it == handlers.end()) {
			fmt::print(stderr,
			           "\033[1;31merror: command `{}` not found "
			           "\033[1;37m[{}]\033[m\n",
			           args.front(), shell::join(args));
			return false;
		}
		auto [min, call] = it->second;
		args = args.subspan(1);
		if (args.size() < min) {
			fmt::print(stderr,
			           "\033[1;31merror: command `{}` expects {}, got {} "
			           "argument{}\033[m\n",
			           args.front(), min, args.size(),
			           args.size() == 1 ? "" : "s");
			return false;
		}
		auto const result = call(handler, args);
		if (!result) {
			if (!can_fail || command != "rm"sv) {
				fmt::print(
				    stderr,
				    "\033[1;31merror: problem while handling `{} {}`\033[m\n",
				    orig, shell::join(args));
			}
			return can_fail;
		}
		return result;
	}

	std::string expand(std::string_view input, std::smatch const& m) {
		std::string result;

		auto it = input.begin();
		auto end = input.end();
		auto start = it;
		while (it != end) {
			while (it != end && *it != '\\')
				++it;

			result.append(start, it);

			if (it == end) continue;
			++it;

			if (it == end || !std::isdigit(static_cast<unsigned char>(*it))) {
				start = it;
				continue;
			}

			unsigned value = 0;
			while (it != end && std::isdigit(static_cast<unsigned char>(*it))) {
				value *= 10;
				value += *it - '0';
				++it;
			}
			result.append(m[value].str());

			start = it;
		}
		return result;
	}

	inline void alt_path(std::string& result, size_t prev_size) {
#ifdef _WIN32
		for (auto index = prev_size; index < result.size(); ++index) {
			auto& c = result[index];
			if (std::isspace(static_cast<unsigned char>(c))) break;
			if (c == '\\') c = '/';
		}
#endif
	}

	std::string replace_var(std::string_view full_input,
	                        std::string_view replaced,
	                        std::string_view var_name) {
		std::string result{};
		bool first = true;
		while (!full_input.empty()) {
			auto pos = full_input.find(replaced);
			auto chunk = full_input.substr(0, pos);
			auto prev_size = result.size();
			if (pos == std::string_view::npos) {
				result.append(chunk);
				if (!first) alt_path(result, prev_size);
				full_input = {};
				continue;
			}
			result.append(chunk);
			if (!first) alt_path(result, prev_size);
			result.append(var_name);
			pos += replaced.size();
			full_input = full_input.substr(pos);
			first = false;
		}
		return result;
	}

	void runtime::fix(
	    std::string& text,
	    std::vector<std::pair<std::string, std::string>> const& patches) const {
		text = replace_var(text, shell::get_u8path(temp_dir), "$TMP");
		text = replace_var(text, shell::get_u8path(rt_target.parent_path()),
		                   "$INST");
		for (auto const& [var, path] : *chai_variables) {
			text = replace_var(text, path, "$" + var);
		}

		if constexpr (fs::path::preferred_separator !=
		              static_cast<fs::path::value_type>('/')) {
			text = replace_var(text, shell::get_generic_path(temp_dir), "$TMP");
			text = replace_var(text,
			                   shell::get_generic_path(rt_target.parent_path()),
			                   "$INST");
			for (auto const& [var, path] : *chai_variables) {
				text = replace_var(
				    text, shell::get_generic_path(shell::make_u8path(path)),
				    "$" + var);
			}
		}

		if (!version.empty()) text = replace_var(text, version, "$VERSION");
		auto lines = split_str(text, '\n');

		std::vector<std::tuple<std::regex, std::string_view, std::string_view>>
		    compiled;
		compiled.reserve(patches.size() + common_patches->size());

		using namespace std::regex_constants;

		for (auto const& [expr, replacement] : *common_patches) {
			try {
				compiled.push_back({std::regex{expr, optimize | ECMAScript},
				                    expr, replacement});
			} catch (std::regex_error const& e) {
				fmt::print(stderr, "common patches: exception: {}\n  {}\n",
				           e.what(), repr(expr));
			}
		}

		for (auto const& [expr, replacement] : patches) {
			try {
				compiled.push_back({std::regex{expr, optimize | ECMAScript},
				                    expr, replacement});
			} catch (std::regex_error const& e) {
				fmt::print(stderr, "json patches: exception: {}\n  {}\n",
				           e.what(), repr(expr));
			}
		}

		for (auto& line : lines) {
			for (auto const& [expr, original, replacement] : compiled) {
				std::smatch m;
				if (std::regex_match(line, m, expr)) {
					line = testbed::expand(replacement, m);
					break;
				}
			}
			// fmt::print("\n");
		}

		text = fmt::to_string(fmt::join(lines, "\n"));
	}
}  // namespace testbed
