// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "testbed/test.hh"
#include <fmt/format.h>
#include "base/diff.hh"
#include "base/shell.hh"
#include "base/str.hh"
#include "io/file.hh"
#include "io/run.hh"

using namespace std::literals;

namespace testbed {
	namespace {
		template <typename Dest>
		Dest get(json::map* map, json::string const& key, Dest def) {
			auto ref = cast<Dest>(map, key);
			if (ref) return *ref;
			return def;
		}

		std::string get(json::map* map,
		                json::string const& key,
		                std::string_view def) {
			auto ref = cast<json::string>(map, key);
			if (ref) return from_u8s(*ref);
			return {def.data(), def.size()};
		}

		std::variant<bool, std::string> get_disabled(json::map* map) {
			auto os = cast<json::string>(map, u8"disabled");
			auto flag = get(map, u8"disabled", false);
			if (os) return from_u8s(*os);
			return flag;
		}

		std::vector<std::pair<std::string, std::string>> patches(
		    json::map const& root) {
			std::vector<std::pair<std::string, std::string>> result{};

			if (auto map = cast<json::map>(root, u8"patches"); map) {
				result.reserve(map->size());

				for (auto const& [key, value] : map->items()) {
					auto const val = cast<json::string>(value);
					if (!val) continue;
					result.push_back({from_u8s(key), from_u8s(*val)});
				}
			}
			if (auto array = cast<json::array>(root, u8"patches"); array) {
				result.reserve(array->size());

				for (auto const& node_row : *array) {
					auto const row = cast<json::array>(node_row);
					if (!row || row->size() < 2) continue;
					auto const key = cast<json::string>(row->at(0));
					auto const value = cast<json::string>(row->at(1));
					if (!key || !value) continue;
					result.push_back({from_u8s(*key), from_u8s(*value)});
				}
			}

			return result;
		}

		std::map<std::string, test_variable> env_variables(
		    json::map const& root) {
			std::map<std::string, test_variable> result{};
			if (auto map = cast<json::map>(root, u8"env"); map) {
				for (auto const& [key, value] : map->items()) {
					if (auto const none = cast<std::nullptr_t>(value); none) {
						result[from_u8s(key)] = nullptr;
					} else if (auto const str = cast<json::string>(value);
					           str) {
						result[from_u8s(key)] = from_u8s(*str);
					} else if (auto const array = cast<json::array>(value);
					           array) {
						std::vector<std::string> addendum;
						addendum.reserve(array->size());

						for (auto const& item : *array) {
							if (auto const str_item = cast<json::string>(item);
							    str_item) {
								addendum.push_back(from_u8s(*str_item));
							}
						}

						result[from_u8s(key)] = std::move(addendum);
					}
				}
			}

			return result;
		}

		enum class split_or_wrap { split, wrap };

		strlist strlist_from_json(json::node const& node,
		                          bool& ok,
		                          split_or_wrap split = split_or_wrap::split) {
			auto str = cast<json::string>(node);
			auto arr = cast<json::array>(node);

			ok = true;
			if (str)
				return split == split_or_wrap::split
				           ? shell::split(from_u8(*str))
				           : strlist{from_u8s(*str)};
			if (!arr) {
				ok = false;
				return {};
			}

			strlist result;
			result.reserve(arr->size());
			for (auto const& subnode : *arr) {
				auto arg = cast<json::string>(subnode);
				if (!arg) {
					ok = false;
					return {};
				}
				result.push_back(from_u8s(*arg));
			}

			return result;
		}

		std::string out_from_node(json::node const& node, bool& ok) {
			auto list = strlist_from_json(node, ok, split_or_wrap::wrap);
			std::string result{};
			if (!ok) return result;

			auto size = list.size() - 1;
			for (auto const& line : list)
				size += line.length();

			result.reserve(size);
			for (auto const& line : list) {
				if (!result.empty()) result.push_back('\n');
				result.append(line);
			}

			return result;
		}

		std::optional<io::capture> expected_from_json(json::node const& node,
		                                              bool& ok) {
			if (std::holds_alternative<std::nullptr_t>(node))
				return std::nullopt;

			ok = false;

			auto arr = cast<json::array>(node);
			if (!arr || arr->size() < 3) return std::nullopt;

			auto return_code = cast<long long>(arr->at(0));
			if (!return_code) return std::nullopt;

			auto ok_stdout = true, ok_stderr = true;
			std::optional<io::capture> result =
			    io::capture{.return_code = static_cast<int>(*return_code),
			                .output = out_from_node(arr->at(1), ok_stdout),
			                .error = out_from_node(arr->at(2), ok_stdout)};
			if (!ok_stdout || !ok_stderr) return std::nullopt;

			ok = true;
			return result;
		}

		std::vector<strlist> commands_from_json(json::map const& src,
		                                        json::string const& key,
		                                        bool& ok) {
			ok = true;
			auto it = src.find(key);
			if (it == src.end()) return {};

			auto single_line = cast<json::string>(it->second);
			if (single_line) return {shell::split(from_u8(*single_line))};

			ok = false;
			auto lines = cast<json::array>(it->second);
			if (!lines) return {};

			std::vector<strlist> result;
			result.reserve(lines->size());
			for (auto const& line : *lines) {
				auto local_ok = true;
				result.push_back(strlist_from_json(line, local_ok));
				if (!local_ok) return {};
			}

			ok = true;
			return result;
		}
	}  // namespace

	struct select_env {
		test* tgt;
		runtime const* saved{tgt->current_rt};

		select_env(test* tgt, runtime const* e) : tgt{tgt} {
			tgt->current_rt = e;
		}
		~select_env() { tgt->current_rt = saved; }
	};

	bool test::run_cmds(runtime const& rt, std::span<strlist const> commands) {
		select_env from{this, &rt};

		for (auto const& cmd : commands) {
			auto expanded = rt.expand(cmd, {}, exp::generic);
			if (!rt.run(*this, expanded.stg)) return false;
		}
		return true;
	}

	std::string test_data::name_for(std::string_view name) {
		auto items_stg = split(name, '-');
		std::span items = items_stg;
		std::string result{};
		result.reserve(name.size() + 2);
		result.push_back('(');
		result.append(items.front());
		result.push_back(')');
		for (auto item : items.subspan(1)) {
			result.push_back(' ');
			result.append(item);
		}
		return result;
	}

	std::string test_data::test_name() const {
		auto const basename = shell::get_path(filename.stem());
		auto const dirname = shell::get_path(filename.parent_path().filename());
		return fmt::format("{} :: {}", name_for(dirname), name_for(basename));
	}

	bool test_data::not_disabled() const {
		if (std::holds_alternative<bool>(disabled))
			return !std::get<bool>(disabled);
#ifdef _WIN32
		static constexpr auto sys_platform = "win32"sv;
#endif
#ifdef __linux__
		static constexpr auto sys_platform = "linux"sv;
#endif
		return std::get<std::string>(disabled) != sys_platform;
	}

	test_data test_data::load(fs::path const& filename,
	                          size_t index,
	                          std::optional<std::string> const& schema,
	                          bool& renovate) {
		renovate = false;

		auto file = io::fopen(filename);
		if (!file) return {.filename = filename, .ok{false}};
		auto data = file.read();
		auto root = json::read_json(
		    {reinterpret_cast<char8_t const*>(data.data()), data.size()});
		auto root_map = cast<json::map>(root);
		if (!root_map) return {.filename = filename, .ok{false}};

		bool ok = true;

		if (schema) {
			auto it = root_map->lower_bound(u8"$schema");
			if (it == root_map->end() || it->first != u8"$schema"sv) {
				root_map->insert_at_front(it, {u8"$schema"s, to_u8s(*schema)});
				renovate = true;
			} else if (auto json_schema = cast<json::string>(it->second);
			           !json_schema || *json_schema != to_u8(*schema)) {
				it->second = to_u8s(*schema);
				renovate = true;
			}
		}

		auto it = root_map->find(u8"args");
		if (it == root_map->end()) return {.filename = filename, .ok{false}};
		auto call_args = strlist_from_json(it->second, ok);
		if (!ok) return {.filename = filename, .ok{false}};

		auto post = commands_from_json(*root_map, u8"post", ok);
		if (!ok) return {.filename = filename, .ok{false}};

		checks check{testbed::check::all, testbed::check::all};
		if (auto json_checks = cast<json::map>(root_map, u8"check");
		    json_checks) {
			std::array stream_ids = {u8"stdin"s, u8"stderr"s};
			for (size_t index = 0; index < check.size(); ++index) {
				auto stream_check =
				    cast<json::string>(json_checks, stream_ids[index]);
				if (!stream_check) continue;
				if (*stream_check == u8"all"sv) continue;
				if (*stream_check == u8"begin"sv) {
					check[index] = testbed::check::begin;
					continue;
				}
				if (*stream_check == u8"end"sv) {
					check[index] = testbed::check::end;
					continue;
				}
				return {.filename = filename, .ok{false}};
			}
		}

		it = root_map->find(u8"expected");
		if (it == root_map->end()) return {.filename = filename, .ok{false}};
		auto expected = expected_from_json(it->second, ok);
		if (!ok) return {.filename = filename, .ok{false}};

		auto lang = get(root_map, u8"lang", "en"sv);
		auto const linear = get(root_map, u8"linear", false);
		auto const disabled = get_disabled(root_map);
		auto env = testbed::env_variables(*root_map);
		auto patches = testbed::patches(*root_map);

		auto prepare = commands_from_json(*root_map, u8"prepare", ok);
		if (!ok) return {.filename = filename, .ok{false}};

		auto cleanup = commands_from_json(*root_map, u8"cleanup", ok);
		if (!ok) return {.filename = filename, .ok{false}};

		return {
		    .filename = filename,
		    .index = index,
		    .data = std::move(*root_map),
		    .lang = std::move(lang),
		    .prepare = std::move(prepare),
		    .call_args = std::move(call_args),
		    .post = std::move(post),
		    .cleanup = std::move(cleanup),
		    .expected = std::move(expected),
		    .linear = linear,
		    .disabled = disabled,
		    .env = std::move(env),
		    .patches = std::move(patches),
		    .check = check,
		};
	}

	std::pair<io::args_storage, std::vector<io::args_storage>>
	test::expand_test_calls(runtime const& rt) const {
		std::pair<io::args_storage, std::vector<io::args_storage>> result{};

		result.first = rt.expand(call_args, stored_env, exp::preferred);
		result.second.reserve(post.size());
		for (auto const& cmd : post) {
			result.second.push_back(rt.expand(cmd, stored_env, exp::preferred));
		}

		return result;
	}

	std::map<std::string, std::string> test::copy_environment_block(
	    std::map<std::string, std::string> const& variables,
	    runtime const& rt) const {
		auto result = variables;
		result["LANGUAGE"] = lang;
		for (auto const& [key, value] : env) {
			if (std::holds_alternative<std::nullptr_t>(value)) {
				result.erase(key);
			} else if (std::holds_alternative<std::string>(value)) {
				result[key] =
				    rt.expand(std::get<std::string>(value), {}, exp::preferred);
			} else if (std::holds_alternative<std::vector<std::string>>(
			               value)) {
				auto const& vars = std::get<std::vector<std::string>>(value);
				for (auto const& var : vars) {
					shell::append(result, key,
					              rt.expand(var, {}, exp::preferred));
				}
			}
		}
		if (needs_mocks_in_path) {
			shell::prepend(result, "PATH"s, rt.mocks_dir());
		}

		return result;
	}

	io::capture test::observe(
	    std::pair<io::args_storage, std::vector<io::args_storage>>& calls,
	    std::map<std::string, std::string> const& variables,
	    runtime const& rt) const {
		auto run_cwd = linear ? nullptr : &cwd();

		if (rt.debug) {
			fmt::print(stderr,
			           "\033[1;33m"
			           "> {} {}\033[m\n",
			           shell::get_generic_path(rt.rt_target),
			           shell::join(calls.first.stg));
		}

		auto result = io::run({
		    .exec = rt.rt_target,
		    .args = calls.first.args(),
		    .cwd = run_cwd,
		    .env = &variables,
		    .pipe = io::pipe::outs,
		});

		for (auto& cmd : calls.second) {
			if (result.return_code) break;

			if (rt.debug) {
				fmt::print(stderr,
				           "\033[1;33m"
				           "> {} {}\033[m\n",
				           shell::get_generic_path(rt.rt_target),
				           shell::join(cmd.stg));
			}

			auto local = io::run({
			    .exec = rt.rt_target,
			    .args = cmd.args(),
			    .cwd = run_cwd,
			    .env = &variables,
			    .pipe = io::pipe::outs,
			});

			result.return_code = local.return_code;

			if (!result.output.empty() && !local.output.empty())
				result.output.push_back('\n');
			result.output.append(local.output);

			if (!result.error.empty() && !local.error.empty())
				result.error.push_back('\n');
			result.error.append(local.error);
		}

		return result;
	}

	std::optional<io::capture> test::run(
	    std::map<std::string, std::string> const& variables,
	    runtime const& rt) {
		// build/.testing/X{16}
		if (!mkdirs(rt.temp_dir)) {
			return std::nullopt;
		}
		if (!rmtree(rt.mocks_dir())) {
			return std::nullopt;
		}

		if (!run_cmds(rt, prepare)) return std::nullopt;
		auto expanded = expand_test_calls(rt);
		auto const local_env = copy_environment_block(variables, rt);

		auto result = observe(expanded, local_env, rt);

		if (!run_cmds(rt, cleanup)) return std::nullopt;

		rt.fix(result.output, patches);
		rt.fix(result.error, patches);

		return {std::move(result)};
	}

	io::capture test::clip(io::capture const& actual) const {
		auto result = actual;
		struct stream_ref {
			testbed::check side;
			std::string* actual;
			std::string const* expected;
		};
		std::array streams{
		    stream_ref{check[0], &result.output, &expected->output},
		    stream_ref{check[1], &result.error, &expected->error},
		};

		for (auto const& stream : streams) {
			if (stream.side == check::all) continue;
			if (stream.side == check::begin) {
				*stream.actual =
				    stream.actual->substr(0, stream.expected->size());
			} else {
				*stream.actual = stream.actual->substr(stream.expected->size());
			}
		}

		return result;
	}

	std::string test::report(io::capture const& clipped,
	                         runtime const& rt) const {
		std::string result;
		if (clipped.return_code != expected->return_code) {
			result += fmt::format(
			    "Return code\n"
			    "  Expected:\n"
			    "    {expected}\n"
			    "  Actual:\n"
			    "    {actual}\n"
			    "\n",
			    fmt::arg("expected", expected->return_code),
			    fmt::arg("actual", clipped.return_code));
		};

		struct stream_ref {
			testbed::check side;
			std::string_view label;
			std::string_view actual;
			std::string_view expected;
		};
		for (auto const& stream : {
		         stream_ref{check[0], "Standard out"sv, clipped.output,
		                    expected->output},
		         stream_ref{check[1], "Standard err"sv, clipped.error,
		                    expected->error},
		     }) {
			if (stream.actual == stream.expected) continue;
			auto const pre_mark = stream.side == check::end ? "..."sv : ""sv;
			auto const post_mark = stream.side == check::begin ? "..."sv : ""sv;

			result += fmt::format(
			    "{label}\n"
			    "  Expected:\n"
			    "    {pre_mark}{expected}{post_mark}\n"
			    "  Actual:\n"
			    "    {pre_mark}{actual}{post_mark}\n"
			    "\n"
			    "Diff:\n"
			    "{diff}\n"
			    "\n",
			    fmt::arg("label", stream.label),
			    fmt::arg("expected", repr(stream.expected)),
			    fmt::arg("actual", repr(stream.actual)),
			    fmt::arg("pre_mark", pre_mark),
			    fmt::arg("post_mark", post_mark),
			    fmt::arg("diff", diff(stream.expected, stream.actual)));
		};

		auto const env = copy_environment_block({}, rt);
		auto const expanded = rt.expand(call_args, stored_env, exp::preferred);
		std::vector<std::string> ran_cmd{};
		ran_cmd.reserve(env.size() + 1 + expanded.link_stg.size());
		for (auto const& [var, value] : env) {
			ran_cmd.push_back(fmt::format("{}={}", var, value));
		}
		ran_cmd.push_back(shell::get_generic_path(rt.rt_target));
		for (auto const& arg : expanded.stg) {
			ran_cmd.push_back(arg);
		}
		result +=
		    fmt::format("{}\ncwd: {}\ntest: {}", shell::join(ran_cmd),
		                shell::get_u8path(cwd()), shell::get_u8path(filename));

		return result;
	}

	void test::nullify(std::optional<std::string> const& lang) {
		if (lang) {
			auto schema = data.find(u8"$schema");
			auto it = data.lower_bound(u8"lang");
			if (it == data.end()) {
				if (schema == data.end())
					data.insert_at_front(it, {u8"lang", to_u8s(*lang)});
				else
					data.insert_after(u8"$schema", it,
					                  {u8"lang", to_u8s(*lang)});
			} else {
				it->second = to_u8s(*lang);
			}
		}
		data.set(u8"expected", nullptr);
		store();
	}

	void test::store() const {
		json::string text;
		json::write_json(text, data,
		                 json::four_spaces.with_horiz_space(HORIZ_SPACE));
		if (text.empty() || text.back() != u8'\n') text.push_back(u8'\n');
		auto file = io::fopen(filename, "wb");
		if (!file) return;
		file.store(text.data(), text.size());
	}
}  // namespace testbed
