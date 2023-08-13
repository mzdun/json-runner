// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#define NOMINMAX

#include "io/run.hh"
#include <Windows.h>
#include <errno.h>
#include <args/parser.hpp>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <thread>
#include <vector>
#include "fmt/format.h"
#include "io/file.hh"
#include "io/path_env.hh"

namespace io {
	namespace {
		using namespace std::literals;

		enum class pipe { output, error, input };

		void append(std::wstring& ws, wchar_t next) { ws.push_back(next); }

		template <size_t Length>
		void append(std::wstring& ws, wchar_t const (&next)[Length]) {
			ws.insert(ws.end(), next, next + Length - 1);
		}

		void append(std::wstring& ws, std::wstring_view next) {
			ws.append(next);
		}

		void append(std::wstring& ws, std::string_view next) {
			ws.reserve(ws.size() + next.size());
			std::copy(next.begin(), next.end(), std::back_inserter(ws));
		}

		template <typename Char>
		void append_arg(std::wstring& ws, std::basic_string_view<Char> next) {
			static constexpr auto npos = std::string_view::npos;

			auto const esc = [=] {
				if constexpr (std::same_as<char, Char>)
					return next.find_first_of("\" ");
				else
					return next.find_first_of(L"\" ");
			}();

			if (esc == npos) {
				append(ws, next);
				return;
			}

			append(ws, '"');
			bool in_slashes = false;
			size_t slashes = 0;
			for (auto c : next) {
				switch (c) {
					case '\\':
						if (!in_slashes) slashes = 0;
						in_slashes = true;
						++slashes;
						break;
					case '"':
						// CommandLineToArgvW: (2n) + 1 backslashes followed by
						// a quotation mark produce n backslashes and ", but
						// does not toggle the "in quotes" mode
						if (in_slashes) {
							for (size_t i = 0; i < slashes; ++i)
								append(ws, '\\');
						}
						append(ws, '\\');
						in_slashes = false;
						break;
					default:
						in_slashes = false;
						break;
				}
				append(ws, c);
			}

			// CommandLineToArgvW: 2n backslashes followed by a quotation mark
			// produce n backslashes and no ", instead toggle the "in quotes"
			// mode
			if (in_slashes) {
				for (size_t i = 0; i < slashes; ++i)
					append(ws, '\\');
			}
			append(ws, '"');
		}

		template <typename Char>
		void append_arg(std::wstring& ws, std::basic_string<Char> const& next) {
			append_arg(ws, std::basic_string_view<Char>{next});
		}

		template <typename Char>
		size_t arg_length(std::basic_string_view<Char> arg) {
			static constexpr auto npos = std::string_view::npos;

			auto length = arg.length();
			auto quot = arg.find('"');
			while (quot != npos) {
				auto slashes = quot;
				while (slashes && arg[slashes - 1] == '\\')
					--slashes;
				// CommandLineToArgvW: (2n) + 1 backslashes followed by a
				// quotation mark produce n backslashes and ", but does not
				// toggle the "in quotes" mode
				length += 1 + slashes;
				quot = arg.find('"', quot + 1);
			}
			auto const escaped = length != arg.length();
			auto const space = !escaped ? arg.find(' ') : npos;
			if (escaped || space != npos) {
				length += 2;
				auto slashes = arg.length();
				while (slashes && arg[slashes - 1] == '\\')
					--slashes;
				// CommandLineToArgvW: 2n backslashes followed by a quotation
				// mark produce n backslashes and no ", instead toggle the "in
				// quotes" mode
				length += slashes;
			}

			return length;
		}

		template <typename Char>
		size_t arg_length(std::basic_string<Char> const& arg) {
			return arg_length(std::basic_string_view<Char>{arg});
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

		// GCOV_EXCL_START[WIN32]
		// Currently, used only in debug fmt::prints
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
		// GCOV_EXCL_STOP

		struct win32_pipe {
			HANDLE read{nullptr};
			HANDLE write{nullptr};

			~win32_pipe() {
				close_side(read);
				close_side(write);
			}

			void close_read() { close_side(read); }
			void close_write() { close_side(write); }

			bool open_pipe(SECURITY_ATTRIBUTES* attrs, std::string& debug) {
				auto const ret = CreatePipe(&read, &write, attrs, 0);
				if (!ret) {
					debug.append(
					    fmt::format("open_pipe: error {:x}\n", GetLastError()));
					return false;
				} else {
					debug.append(fmt::format("open_pipe -> read:{} write:{}\n",
					                         read, write));
				}
				return ret;
			}

			bool open_std(pipe direction,
			              SECURITY_ATTRIBUTES* attrs,
			              std::string& debug) {
				auto handle = GetStdHandle(
				    direction == pipe::input    ? STD_INPUT_HANDLE
				    : direction == pipe::output ? STD_OUTPUT_HANDLE
				                                : STD_ERROR_HANDLE);
				if (handle) {
					HANDLE dup{};
					if (DuplicateHandle(GetCurrentProcess(), handle,
					                    GetCurrentProcess(), &dup, 0, TRUE,
					                    DUPLICATE_SAME_ACCESS))
						handle = dup;
					if (direction == pipe::input)
						read = handle;
					else
						write = handle;
					return true;
				}

				return open_pipe(attrs, debug);
			}

			bool open_devnull(SECURITY_ATTRIBUTES* attrs, std::string& debug) {
				write = ::CreateFileW(L"nul", GENERIC_READ | GENERIC_WRITE,
				                      FILE_SHARE_READ | FILE_SHARE_WRITE, attrs,
				                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
				                      nullptr);
				debug.append(fmt::format("open_devnull -> fd:{}\n", write));
				return true;
			}

			bool open(std::optional<std::string_view> const& input,
			          pipe,
			          SECURITY_ATTRIBUTES* attrs,
			          std::string& debug) {
				if (!input) return true;
				debug.append("input: ");
				return open_pipe(attrs, debug);
			}

			bool open(stream_decl const& decl,
			          pipe direction,
			          SECURITY_ATTRIBUTES* attrs,
			          std::string& debug) {
				switch (direction) {
					case pipe::input:
						debug.append("input?!?");
						break;
					case pipe::output:
						debug.append("output");
						break;
					case pipe::error:
						debug.append("error");
						break;
				}
				debug.append(": ");
				if (decl == nullptr) debug.append("nothing: ");
				if (decl == piped{}) debug.append("piped: ");
				if (decl == devnull{}) debug.append("/dev/null: ");
				if (decl == redir_to_output{}) debug.append(">&1: ");
				if (decl == redir_to_error{}) debug.append(">&2: ");
				if (decl == terminal{}) debug.append("pty: ");

				if (decl == piped{}) return open_pipe(attrs, debug);
				if (decl == terminal{}) return open_pipe(attrs, debug);
				if (decl == devnull{}) return open_devnull(attrs, debug);

				if (decl == redir_to_output{}) {
					debug.append(fmt::format(
					    "{} to output, {}\n",
					    direction == pipe::error ? "error"sv : "output"sv,
					    direction == pipe::error ? "ok"sv : "will fail"sv));
					return direction == pipe::error;
				}
				if (decl == redir_to_error{}) {
					debug.append(fmt::format(
					    "{} to error, {}\n",
					    direction == pipe::output ? "output"sv : "error"sv,
					    direction == pipe::output ? "ok"sv : "will fail"sv));
					return direction == pipe::output;
				}

				debug.append("<true>\n");
				return true;
			}

			static constexpr DWORD BUFSIZE = 16384u;

			std::thread async_write(std::string_view src) {
				return std::thread(
				    [](win32_pipe* self, std::string_view bytes) {
					    auto ptr = bytes.data();
					    auto size = bytes.size();
					    DWORD written{};

					    auto const handle = self->write;
					    while (size) {
						    auto chunk = size;
						    if (chunk > BUFSIZE) chunk = BUFSIZE;
						    if (!WriteFile(handle, ptr,
						                   static_cast<DWORD>(chunk), &written,
						                   nullptr)) {
							    // GCOV_EXCL_START
							    [[unlikely]];
							    break;
							    // GCOV_EXCL_STOP
						    }  // GCOV_EXCL_LINE
						    ptr += written;
						    size -= static_cast<size_t>(written);
					    }

					    self->close_write();
				    },
				    this, std::ref(src));
			}

			std::thread async_read(std::string& dst) {
				return std::thread(
				    [](HANDLE handle, std::string& bytes) {
					    static constexpr auto CR = '\r';

					    DWORD read;
					    std::vector<char> buffer(BUFSIZE);

					    for (;;) {
						    if (!ReadFile(handle, buffer.data(), BUFSIZE, &read,
						                  nullptr) ||
						        read == 0)
							    break;

						    auto first = buffer.data();
						    auto last = first + read;
						    bytes.reserve(bytes.size() + read);

						    while (first != last) {
							    auto start = first;
							    while (first != last && *first != CR)
								    ++first;

							    bytes.insert(bytes.end(), start, first);

							    if (first != last) ++first;
						    }
					    }
				    },
				    read, std::ref(dst));
			}

		private:
			static void close_side(HANDLE& handle) {
				if (handle != nullptr) CloseHandle(handle);
				handle = nullptr;
			}
		};

		struct win32_pipes {
			win32_pipe input{};
			win32_pipe output{};
			win32_pipe error{};

			bool open(run_opts const& opts, std::string& debug) {
				SECURITY_ATTRIBUTES saAttr{
				    .nLength = sizeof(SECURITY_ATTRIBUTES),
				    .lpSecurityDescriptor = nullptr,
				    .bInheritHandle = TRUE,
				};

#define OPEN(DIRECTION)                                                     \
	if (!DIRECTION.open(opts.DIRECTION, pipe::DIRECTION, &saAttr, debug)) { \
		[[unlikely]];                                                       \
		return false;                                                       \
	}

				OPEN(input);
				OPEN(output);
				OPEN(error);
				return true;
			}

			std::string io(std::optional<std::string_view> const& input_data,
			               capture& output_data) {
				input.close_read();
				output.close_write();
				error.close_write();

				{
					HANDLE handles[] = {output.read, error.read};
					WaitForMultipleObjects(2, handles, FALSE, INFINITE);
				}

				std::vector<std::thread> threads{};
				threads.reserve(
				    (input.write != nullptr && input_data ? 1u : 0u) +
				    (output.read != nullptr ? 1u : 0u) +
				    (error.read != nullptr ? 1u : 0u));

				if (input.write != nullptr && input_data)
					threads.push_back(input.async_write(*input_data));

				if (output.read != nullptr)
					threads.push_back(output.async_read(output_data.output));

				if (error.read != nullptr)
					threads.push_back(error.async_read(output_data.error));

				for (auto& thread : threads) {
					thread.join();
				}

				std::string result;
				return result;
			}
		};

		struct win32_handle_attributes {
			std::vector<char> buffer{};
			std::vector<HANDLE> handles{};
			std::unique_ptr<_PROC_THREAD_ATTRIBUTE_LIST,
			                decltype([](LPPROC_THREAD_ATTRIBUTE_LIST list) {
				                DeleteProcThreadAttributeList(list);
			                })>
			    attribute_list{nullptr};

			LPPROC_THREAD_ATTRIBUTE_LIST include_handles(
			    win32_pipes const& pipes) {
				handles.clear();
				handles.reserve(3);
				if (pipes.input.read) handles.push_back(pipes.input.read);
				if (pipes.output.write) handles.push_back(pipes.output.write);
				if (pipes.error.write) handles.push_back(pipes.error.write);

				if (handles.empty()) return nullptr;

				SIZE_T size = 0;
				if (!InitializeProcThreadAttributeList(nullptr, 1, 0, &size) &&
				    GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
					return nullptr;
				}

				buffer.resize(size);
				auto local = (LPPROC_THREAD_ATTRIBUTE_LIST)buffer.data();
				size = buffer.size();

				if (!InitializeProcThreadAttributeList(local, 1, 0, &size)) {
					auto const err = GetLastError();
					fmt::print(stderr,
					           "InitializeProcThreadAttributeList: {}\n", err);
					return nullptr;
				}

				attribute_list.reset(local);

				if (!UpdateProcThreadAttribute(
				        local, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				        handles.data(), handles.size() * sizeof(HANDLE), NULL,
				        NULL)) {
					auto const err = GetLastError();
					fmt::print(stderr, "UpdateProcThreadAttribute: {}\n", err);
				}

				return local;
			}
		};

		struct scripted_file {
			fs::path program_file;
			std::string script_engine;
			bool access{true};

			std::wstring command_line(std::wstring const& program,
			                          args::arglist args) const {
				auto length = script_engine.empty() ? arg_length(program)
				                                    : arg_length(script_engine);
				if (!script_engine.empty())
					length += 1 + arg_length(program_file.native());
				for (unsigned index = 0; index < args.size(); ++index)
					length += 1 + arg_length(args[index]);

				std::wstring arg_string;
				arg_string.reserve(length + 1);

				if (script_engine.empty()) {
					append_arg(arg_string, program);
				} else {
					append_arg(arg_string, script_engine);
					append(arg_string, ' ');
					append_arg(arg_string, program_file.native());
				}

				for (unsigned index = 0; index < args.size(); ++index) {
					append(arg_string, ' ');
					append_arg(arg_string, from_utf8(args[index]));
				}

				append(arg_string,
				       ' ');  // because we need .data() in CreateProcess...
				return arg_string;
			}

			LPCWSTR command_file() const {
				return script_engine.empty() ? program_file.c_str() : nullptr;
			}
		};
		std::wstring env(wchar_t const* name) {
			wchar_t* env{};
			size_t length{};
			auto err = _wdupenv_s(&env, &length, name);
			std::wstring result{};
			if (!err && env && length) result.assign(env, length - 1);
			return result;
		}
		std::wstring filename(std::wstring_view program,
		                      std::wstring_view ext) {
			std::wstring result{};
			result.reserve(program.length() + ext.length());
			result.append(program);
			result.append(ext);
			return result;
		}

		bool file_exists(fs::path const& path) {
			std::error_code ec{};
			auto status = fs::status(path, ec);
			return !ec && fs::is_regular_file(status);
		}

		std::vector<std::wstring> all_lower(
		    std::span<std::wstring_view const> items) {
			std::vector<std::wstring> result{};
			result.reserve(items.size());
			for (auto view : items) {
				std::wstring arg;
				arg.assign(view);
				CharLowerW(arg.data());
				result.push_back(std::move(arg));
			}
			return result;
		}

		fs::path where(fs::path const& hint,
		               wchar_t const* environment_variable,
		               std::wstring const& program) {
			auto ext_str = env(L"PATHEXT");
			auto path_ext = all_lower(split<wchar_t>({}, ext_str));

			if (program.find_first_of(L"\\/"sv) != std::string::npos) {
				return program;
			}

			auto path_str = env(environment_variable);
			auto dirs = split<wchar_t>(hint, path_str);

			for (auto const& dir : dirs) {
				for (auto const ext : path_ext) {
					auto path = fs::path{dir} / filename(program, ext);
					if (file_exists(path)) {
						return path;
					}
				}
			}

			return {};
		}

		fs::path extensionless_where(wchar_t const* environment_variable,
		                             std::wstring const& program) {
			auto path_str = env(environment_variable);
			auto dirs = split<wchar_t>({}, path_str);

			for (auto const& dir : dirs) {
				auto path = fs::path{dir} / program;
				if (file_exists(path)) {
					return fs::canonical(path);
				}
			}

			return {};
		}

		scripted_file locate_file(wchar_t const* environment_variable,
		                          std::wstring const& program) {
			// .exe -> extensionless python
			scripted_file result{};
			result.program_file = where({}, environment_variable, program);
			if (result.program_file.empty()) {
				result.program_file =
				    extensionless_where(environment_variable, program);
				auto in = io::fopen(result.program_file);
				if (!in) {
					result.program_file.clear();
				} else {
					auto first_line = in.read_line();
					if (first_line.starts_with("#!"sv) &&
					    first_line.find("python"sv) != std::string::npos) {
						result.script_engine.assign("python"sv);
					} else {
						result.program_file.clear();
						result.access = false;
					}
				}
			}
			return result;
		}
	}  // namespace

	capture run(run_opts const& options) {
		capture result{};

		auto const path = locate_file(L"PATH", options.exec);
		if (path.program_file.empty()) {
			result.return_code = !path.access ? -EACCES : -ENOENT;
			return result;
		}

		std::string debug{};
		win32_pipes pipes{};
		win32_handle_attributes attrs{};
		STARTUPINFOEXW si;
		PROCESS_INFORMATION pi;

		ZeroMemory(&si, sizeof(si));

		if (!pipes.open(options, debug)) {
			// GCOV_EXCL_START
			[[unlikely]];
			debug.append("pipes did not open\n");
			result.return_code = 128;
			return result;
		}  // GCOV_EXCL_STOP

		si.StartupInfo.cb = sizeof(si);

		si.lpAttributeList = attrs.include_handles(pipes);
		if (si.lpAttributeList) {
			si.StartupInfo.hStdInput = pipes.input.read;
			si.StartupInfo.hStdOutput = pipes.output.write;
			si.StartupInfo.hStdError = pipes.error.write;
			si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
		}
		ZeroMemory(&pi, sizeof(pi));

		LPVOID environment = nullptr;
		std::vector<wchar_t> environment_stg;
		if (options.env) {
			for (auto const& [key, value] : *options.env) {
				auto wkey = from_utf8(key);
				auto wvalue = from_utf8(value);
				environment_stg.insert(environment_stg.end(), wkey.begin(),
				                       wkey.end());
				environment_stg.push_back(L'=');
				environment_stg.insert(environment_stg.end(), wvalue.begin(),
				                       wvalue.end());
				environment_stg.push_back(0);
			}
			environment_stg.push_back(0);
			environment = environment_stg.data();
		}

		if (!CreateProcessW(
		        path.command_file(),
		        path.command_line(options.exec, options.args).data(), nullptr,
		        nullptr, si.lpAttributeList != nullptr,
		        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
		        environment, options.cwd ? options.cwd->c_str() : nullptr,
		        (LPSTARTUPINFOW)&si, &pi)) {
			// GCOV_EXCL_START[WIN32]
			[[unlikely]];
			auto const error = GetLastError();
			if (error == ERROR_FILE_NOT_FOUND ||
			    error == ERROR_PATH_NOT_FOUND) {
				result.return_code = -ENOENT;
				return result;
			}

			fmt::print(stderr, "CreateProcess: {}\n", error);

			result.return_code = 128;
			return result;
			// GCOV_EXCL_STOP[WIN32]
		}  // GCOV_EXCL_LINE

		debug.append(pipes.io(options.input, result));

		DWORD return_code{};
		WaitForSingleObject(pi.hProcess, INFINITE);
		if (!GetExitCodeProcess(pi.hProcess, &return_code)) {
			// GCOV_EXCL_START[WIN32]
			[[unlikely]];
			result.return_code = 128;
			return result;
			// GCOV_EXCL_STOP[WIN32]
		}  // GCOV_EXCL_LINE

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		result.return_code = static_cast<int>(return_code);
		return result;
	}

	std::optional<fs::path> find_program(std::span<std::string const> names,
	                                     fs::path const& hint) {
		for (auto const& name : names) {
			auto candidate = where(hint, L"PATH", from_utf8(name));
			if (!candidate.empty()) return candidate;
		}
		return std::nullopt;
	}
}  // namespace io
