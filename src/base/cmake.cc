// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "base/cmake.hh"
#include <optional>
#include <span>
#include "io/file.hh"

#include <fmt/format.h>

namespace fs = std::filesystem;
using namespace std::literals;

namespace cmake {
	enum class tok { STR, OPEN, CLOSE, IDENT };

	struct token {
		tok type;
		std::string_view::iterator reset;
		std::string_view value{};
	};

	struct command {
		std::string name{};
		std::vector<std::string> args{};
	};

	unsigned char uchar(char c) { return c; }

	void skip_ws(std::string_view::iterator& it,
	             std::string_view::iterator end) {
		while (it != end) {
			while (it != end && std::isspace(uchar(*it)))
				++it;
			if (it == end) continue;
			if (*it != '#') break;
			while (it != end && *it != '\n')
				++it;
		}
	}

	struct token_stream {
		std::string_view::iterator it;
		std::string_view::iterator end;
		token_stream(std::string_view file)
		    : it{file.begin()}, end{file.end()} {}

		void put_back(std::optional<token> const& tok) {
			if (tok) it = tok->reset;
		}
		std::optional<token> next() {
			while (it != end) {
				skip_ws(it, end);
				if (it == end) continue;
				if (*it == '(') {
					auto reset = it;
					++it;
					return token{.type = tok::OPEN, .reset = reset};
				}
				if (*it == ')') {
					auto reset = it;
					++it;
					return token{.type = tok::CLOSE, .reset = reset};
				}
				if (*it == '"') {
					auto reset = it;
					++it;
					auto str_start = it;
					while (it != end && *it != '"')
						++it;
					auto str_end = it;
					if (it != end) ++it;
					return token{.type = tok::STR,
					             .reset = reset,
					             .value{str_start, str_end}};
				}

				auto ident_start = it;
				while (it != end && *it != '"' && *it != '(' && *it != ')' &&
				       *it != '#' && !std::isspace(uchar(*it)))
					++it;
				auto ident_end = it;
				return token{.type = tok::IDENT,
				             .reset = ident_start,
				             .value{ident_start, ident_end}};
			}

			return std::nullopt;
		}
	};

	struct command_stream {
		std::vector<std::byte> bytes;
		token_stream tokens{std::string_view{
		    reinterpret_cast<char const*>(bytes.data()), bytes.size()}};

		explicit command_stream(fs::path const& filename)
		    : bytes{open_file(filename)} {}

		std::optional<command> next() {
			while (auto next = tokens.next()) {
				if (next->type == tok::IDENT) return get_command(next->value);
			}
			return std::nullopt;
		}

	private:
		static std::vector<std::byte> open_file(fs::path const& filename) {
			auto file = io::fopen(filename);
			if (!file) return {};

			return file.read();
		}

		command get_command(std::string_view name) {
			command result{};
			result.name.assign(name);
			auto next = tokens.next();
			if (!next || next->type != tok::OPEN) {
				tokens.put_back(next);
				return result;
			}
			while (next = tokens.next()) {
				bool done = false;
				switch (next->type) {
					case tok::CLOSE:
						done = true;
						break;
					case tok::STR:
					case tok::IDENT:
						result.args.push_back(
						    {next->value.data(), next->value.size()});
				}
				if (done) break;
			}
			return result;
		}
	};

	project load_project() {
		std::optional<std::string> project_name;
		std::optional<std::string> version_stability;
		auto version = "0.1.0"s;
		auto description = ""s;

		command_stream commands{"CMakeLists.txt"};
		while (auto cmd = commands.next()) {
			if (cmd->name == "project") {
				project_name = cmd->args[0];
				auto args = std::span{cmd->args}.subspan(1);
				std::optional<std::string_view> var{std::nullopt};
				for (auto const& arg : args) {
					if (!var) {
						var = std::string_view{arg};
						continue;
					}
					auto var_name = *var;
					var = std::nullopt;
					if (var_name == "DESCRIPITON"sv) {
						description.assign(arg);
					} else if (var_name == "VERSION"sv) {
						version.assign(arg);
					}
				}
				if (version_stability) break;
				continue;
			}
			if (cmd->name == "set") {
				auto const& var_name = cmd->args[0];
				if (var_name == "PROJECT_VERSION_STABILITY"sv) {
					version_stability = cmd->args[1];
					if (project_name) break;
				}
			}
		}

		if (!project_name) project_name = ""s;
		if (!version_stability) version_stability = ""s;

		return {.name = std::move(*project_name),
		        .version = std::move(version),
		        .stability = std::move(*version_stability),
		        .description = std::move(description)};
	}

	project const& get_project() {
		static project pro{load_project()};
		return pro;
	}
}  // namespace cmake
