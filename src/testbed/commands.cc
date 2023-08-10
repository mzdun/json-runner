// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "testbed/commands.hh"
#include <fmt/format.h>
#include <arch/io/file.hh>
#include <arch/unpacker.hh>
#include "base/shell.hh"
#include "base/str.hh"
#include "io/file.hh"
#include "io/run.hh"
#include "testbed/test.hh"

using namespace std::literals;

namespace testbed {
	namespace {
		bool run_tool(fs::path const& name,
		              std::span<std::string const> args,
		              fs::path const& cwd) {
			io::args_storage copy{.stg{args.begin(), args.end()}};
			auto const return_code =
			    io::call({.exec = name, .args = copy.args(), .cwd = &cwd});
			return return_code == 0;
		}

		std::optional<std::string> text_from(fs::path const& filename) {
			auto file = io::fopen(filename);
			if (!file) return std::nullopt;
			auto data = file.read();
			return {std::string{reinterpret_cast<char const*>(data.data()),
			                    data.size()}};
		}
	}  // namespace

	commands::~commands() = default;

	bool commands::cp(fs::path const& src, fs::path const& dst) const {
		std::error_code ec{};
		fs::copy(path(src), path(dst),
		         fs::copy_options::recursive | fs::copy_options::copy_symlinks |
		             fs::copy_options::create_hard_links,
		         ec);
		if (ec) {
			ec.clear();
			fs::copy(
			    path(src), path(dst),
			    fs::copy_options::recursive | fs::copy_options::copy_symlinks,
			    ec);
		}
		return !ec;
	}

	bool commands::cd(fs::path const& dir) {
		cwd_ /= dir;
		return true;  // fs::is_directory(cwd_);
	}

	template <typename Permissions>
	Permissions readonly(Permissions current) {
#ifdef _WIN32
		return Permissions::_File_attribute_readonly;
#else
		static constexpr auto all_write =
		    std::to_underlying(Permissions::owner_write) |
		    std::to_underlying(Permissions::group_write) |
		    std::to_underlying(Permissions::others_write);
		return static_cast<Permissions>(std::to_underlying(current) &
		                                ~all_write);
#endif
	}

	bool commands::make_ro(fs::path const& filename) const {
		auto localized = path(filename);
		std::error_code ec{};
		auto status = fs::status(localized, ec);
		if (ec) return false;
		auto const perms = readonly(status.permissions());
		fs::permissions(localized, perms, ec);
		if (ec) return false;
		return true;
	}

	bool rmtree_silent = false;
	bool commands::mkdirs(fs::path const& dirname) const {
		auto localized = path(dirname);
		std::error_code ec{};
		fs::create_directories(localized, ec);
		return !ec;
	}

	bool commands::rmtree(fs::path const& dirname) const {
		auto localized = path(dirname);
		std::error_code ec{};
		fs::remove_all(localized, ec);
		return !ec;
	}

	bool commands::touch(fs::path const& filename,
	                     std::string const* content) const {
		auto localized = path(filename);
		std::error_code ec{};
		fs::create_directories(localized.parent_path(), ec);
		if (ec) return false;
		auto file = io::fopen(localized, "w");
		if (!file) return false;
		if (content) {
			return file.store(content->data(), content->size()) ==
			       content->size();
		}
		return true;
	}

	class expand_unpacker : public arch::unpacker {
	public:
		using arch::unpacker::unpacker;
		void on_error(fs::path const& filename,
		              char const* msg) const override {
			fmt::print(stderr, "expand: {}: {}\n", shell::get_u8path(filename),
			           msg);
		}
		void on_note(char const* msg) const override {
			fmt::print(stderr, "        note: {}\n", msg);
		}
	};

	bool commands::unpack(fs::path const& filename, fs::path const& dst) const {
		expand_unpacker unp{path(dst)};
		auto file = arch::io::file::open(path(filename));
		if (!file) {
			unp.on_error(filename, "file not found");
			return false;
		}

		arch::base::archive::ptr archive{};
		auto const result = arch::open(std::move(file), archive);
		switch (result) {
			case arch::open_status::compression_damaged:
				unp.on_error(filename, "file compression damaged");
				return false;
			case arch::open_status::archive_damaged:
				unp.on_error(filename, "archive damaged");
				return false;
			case arch::open_status::archive_unknown:
				unp.on_error(filename, "unrecognized archive");
				return false;
			case arch::open_status::ok:
				break;
		}

		if (!archive)
			return unp.on_error(filename, "unknown internal issue"), false;

		return unp.unpack(*archive);
	}

	bool commands::shell() const {
#ifdef _WIN32
		auto shell_name = io::find_program(std::array{"pwsh"s, "cmd"s}, {});
#endif
#ifdef __linux__
		auto shell_name = io::find_program(std::array{"bash"s, "sh"s}, {});
#endif
		if (!shell_name) return false;
		auto name = shell::get_path(shell_name->
#ifdef _WIN32
		                            stem()
#else
		                            filename()
#endif
		);

		auto dashes = std::string(name.length(), '-');
#define COLOR "32"
		fmt::print("\n\033[0;" COLOR
		           "m"
		           "> starting shell: \033[1;" COLOR "m{}\033[m\n\n",
		           name);

		io::run({.exec = *shell_name, .cwd = &cwd()});
		return true;
	}

	std::map<std::string, handler_info> commands::handlers() {
#define HANDLER(METHODCALL)                                    \
	[](commands& handler, std::span<std::string const> args) { \
		return handler.METHODCALL;                             \
	}

#define P(ID) shell::make_u8path(args[ID])
#define P0 P(0)
#define P1 P(1)
#define S(ID) args[ID]
#define S0 S(0)
#define S1 S(1)
#define S2 S(2)
#define A(OFFSET) args.subspan(OFFSET)
#define A1 A(1)
#define A2 A(2)

		return {
		    {"cd"s, {.handler = HANDLER(cd(P0))}},
		    {"cp"s, {.min_args = 2, .handler = HANDLER(cp(P0, P1))}},
		    {"ro"s, {.handler = HANDLER(make_ro(P0))}},
		    {"mkdirs"s, {.handler = HANDLER(mkdirs(P0))}},
		    {"rm"s, {.handler = HANDLER(rmtree(P0))}},
		    {"touch"s,
		     {.handler =
		          [](commands& handler, std::span<std::string const> args) {
			          auto content = args.size() > 1 ? &S1 : nullptr;
			          return handler.touch(P0, content);
		          }}},
		    {"unpack"s, {.min_args = 2, .handler = HANDLER(unpack(P0, P1))}},
		    {"store"s,
		     {.min_args = 2, .handler = HANDLER(store_variable(S0, A1))}},
		    {"mock"s, {.min_args = 2, .handler = HANDLER(mock(S0, S1))}},
		    {"generate"s,
		     {.min_args = 3, .handler = HANDLER(generate(S0, S1, A2))}},
		    {"shell"s, {.min_args = 0, .handler = HANDLER(shell())}},
		};
	}

	bool test::cd(fs::path const& dir) {
		if (!commands::cd(dir)) return false;
		if (linear) {
			std::error_code ec{};
			fs::current_path(cwd(), ec);
			if (ec) return false;
		}
		return true;
	}

	bool test::store_variable(std::string const& var,
	                          std::span<std::string const> call) {
		if (call.empty()) return false;
		auto const& exec_str = call.front();
		call = call.subspan(1);
		auto exec = exec_str == "cov" ? current_rt->rt_target
		                              : shell::make_u8path(exec_str);
		io::args_storage copy{.stg{call.begin(), call.end()}};
		auto proc = io::run({.exec = exec,
		                     .args = copy.args(),
		                     .cwd = &cwd(),
		                     .pipe = io::pipe::output});
		if (proc.return_code != 0) return false;
		auto output = trim(proc.output);
		stored_env[var] = {output.data(), output.size()};
		if (current_rt->debug) fmt::print("  {} {}\n", var, repr(proc.output));
		return true;
	}

	bool test::mock(std::string const& exe, std::string const& link) {
#ifdef _WIN32
		auto prog_name = exe;
		auto link_name = link;
		auto ext = shell::make_u8path(prog_name).extension() == L".exe"sv
		               ? ""sv
		               : ".exe"sv;
		if (!ext.empty()) {
			prog_name.append(ext);
			link_name.append(ext);
		}
#else
		auto const prog_name = std::string_view{exe};
		auto const link_name = std::string_view{link};
#endif
		auto src = current_rt->build_dir / "mocks"sv / prog_name;
		auto dst = current_rt->mocks_dir() / link_name;
		std::error_code ec{};
		fs::create_directories(dst.parent_path(), ec);
		ec.clear();
		fs::remove(dst, ec);
		ec.clear();
		fs::copy(src, dst, fs::copy_options::create_symlinks, ec);
		if (ec) return false;
		needs_mocks_in_path = true;
		return true;
	}

	bool test::generate(std::string const& tmplt,
	                    std::string const& dst,
	                    std::span<std::string const> args) {
		auto file = io::fopen(path(tmplt));
		if (!file) return false;
		auto const tmplt_bytes = file.read();
		file.close();

		std::map<std::string, std::string> vars{};
		for (auto const& arg : args) {
			auto const enter = arg.find('=');

			if (enter == std::wstring_view::npos) {
				vars[arg];
			} else {
				auto var = arg.substr(0, enter);
				auto value = arg.substr(enter + 1);
#ifdef _WIN32
				if (var == "COMPILER"sv) {
					if (shell::make_u8path(value).extension() != L".exe"sv) {
						value.append(".exe"sv);
					}
				}
#endif
				vars[std::move(var)] = std::move(value);
			}
		}

		auto text = current_rt->expand(
		    {reinterpret_cast<char const*>(tmplt_bytes.data()),
		     tmplt_bytes.size()},
		    vars, exp::preferred);

		auto result = path(dst);
		std::error_code ec{};
		fs::create_directories(result.parent_path(), ec);
		if (ec) return false;

		file = io::fopen(result, "w");
		if (!file) return false;
		return file.store(text.data(), text.size()) == text.size();
	}
}  // namespace testbed
