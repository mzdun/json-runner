// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "io/run.hh"
#include <fcntl.h>
#include <fmt/format.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <args/parser.hpp>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>
#include "base/str.hh"
#include "io/path_env.hh"

// define STDOUT_DUMP

#ifdef RUNNING_GCOV
extern "C" {
#include <gcov.h>
}
#endif

#ifdef RUNNING_LLVM_COV
extern "C" int __llvm_profile_write_file(void);
#endif

using namespace std::literals;

namespace io {
	namespace {
		enum class pipe { output, error, input };

		std::string env(char const* name) {
			auto value = getenv(name);
			return value ? value : std::string{};
		}

		bool executable(std::filesystem::path const& program) {
			return !std::filesystem::is_directory(program) &&
			       !access(program.c_str(), X_OK);
		}

		std::filesystem::path where(std::filesystem::path const& bin,
		                            char const* environment_variable,
		                            std::string const& program) {
			if (program.find('/') != std::string::npos) return program;

			auto path_str = env(environment_variable);
			auto dirs = split(bin.native(), path_str);

			for (auto const& dir : dirs) {
				auto path = std::filesystem::path{dir} / program;
				if (executable(path)) {
					return path;
				}
			}

			return {};
		}

		struct pipe_type {
			int read{-1};
			int write{-1};
			std::string debug;

			~pipe_type() {
				close_side(read);
				close_side(write);
			}

			void close_read() { close_side(read); }
			void close_write() { close_side(write); }

			bool open_pipe(std::string& debug) {
				int fd[2];
				if (::pipe(fd) == -1) {
					debug.append(fmt::format("open_pipe: error {}\n", errno));
					return false;
				}
				read = fd[0];
				write = fd[1];
				debug.append(fmt::format("open_pipe -> read:{} write:{}\n",
				                         read, write));
				return true;
			}

			bool open_std(pipe direction) {
				auto fd = direction == pipe::input    ? 0
				          : direction == pipe::output ? 1
				                                      : 2;
				if (direction == pipe::input)
					read = fd;
				else
					write = fd;
				return true;
			}

			bool open_devnull(std::string& debug) {
				write = ::open("/dev/null", O_RDWR);
				debug.append(fmt::format("open_devnull -> fd:{}\n", write));
				return true;
			}

			bool open(std::optional<std::string_view> const& input,
			          pipe,
			          std::string& debug) {
				if (!input) return true;
				debug.append("input: ");
				return open_pipe(debug);
			}

			bool open(stream_decl const& decl,
			          pipe direction,
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
				if (decl == future_terminal{}) debug.append("pty: ");

				if (decl == piped{} || decl == future_terminal{}) {
					return open_pipe(debug);
				}
				if (decl == devnull{}) {
					return open_devnull(debug);
				}
				debug.append("<true>\n");
				return true;
			}

			static constexpr size_t BUFSIZE = 16384u;

			std::thread async_write(std::string_view src) {
				return std::thread(
				    // GCOV_EXCL_START[POSIX]
				    [](pipe_type* self, std::string_view bytes) {
					    // GCOV_EXCL_STOP
					    auto ptr = bytes.data();
					    auto size = bytes.size();

					    auto const fd = self->write;
					    while (size) {
						    auto chunk = size;
						    if (chunk > BUFSIZE) chunk = BUFSIZE;
						    auto actual = ::write(fd, ptr, chunk);
						    if (actual < 0) break;
						    ptr += actual;
						    size -= static_cast<size_t>(actual);
					    }

					    self->close_write();
				    },
				    this, std::ref(src));
			}

#ifdef STDOUT_DUMP
			static std::string dump(std::span<char const> buffer) {
				static constexpr auto len = 20zu;
				char line[len * 4 + 2];
				line[len * 4] = '\n';
				line[len * 4 + 1] = 0;
				auto index = 0zu;
				std::string result{};
				result.reserve(buffer.size() * 4 + buffer.size() / len + 1);

				for (auto b : buffer) {
					if (index == len) {
						result.append(line);
						index = 0;
					}

					static constexpr char alphabet[] = "0123456789ABCDEF";
					auto const c = static_cast<unsigned char>(b);
					line[index * 3] = alphabet[(c >> 4) & 0xF];
					line[index * 3 + 1] = alphabet[c & 0xF];
					line[index * 3 + 2] = ' ';
					line[len * 3 + index] =
					    std::isprint(c) ? static_cast<char>(c) : '.';
					++index;
				}
				if (index < len) {
					for (; index < len; ++index) {
						line[index * 3] = ' ';
						line[index * 3 + 1] = ' ';
						line[index * 3 + 2] = ' ';
						line[len * 3 + index] = ' ';
					}
					result.append(line);
				}

				return result;
			}
#endif

			std::thread async_read(std::string& dst, std::string_view name) {
				return std::thread(
				    [name](pipe_type* self, std::string& bytes) {
					    char buffer[BUFSIZE];
					    auto fd = self->read;

					    while (true) {
						    auto const actual =
						        ::read(fd, buffer, std::size(buffer));
						    if (actual <= 0) break;
#ifdef STDOUT_DUMP
						    self->debug.append(fmt::format("> {}:\n", name));
						    self->debug.append(dump({buffer, buffer + actual}));
#endif
						    bytes.insert(bytes.end(), buffer, buffer + actual);
					    }
				    },
				    this, std::ref(dst));
			}

		private:
			void close_side(int& side) {
				if (side >= 0) ::close(side);
				side = -1;
			}
		};

		struct pipes_type {
			pipe_type input{};
			pipe_type output{};
			pipe_type error{};

			bool open(run_opts const& opts, std::string& debug) {
#define OPEN(DIRECTION)                                            \
	if (!DIRECTION.open(opts.DIRECTION, pipe::DIRECTION, debug)) { \
		[[unlikely]];                                              \
		return false;                                              \
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

				std::vector<std::thread> threads{};
				threads.reserve((input.write > -1 && input_data ? 1u : 0u) +
				                (output.read > -1 ? 1u : 0u) +
				                (error.read > -1 ? 1u : 0u));

				if (input.write > -1 && input_data)
					threads.push_back(input.async_write(*input_data));

				if (output.read > -1)
					threads.push_back(
					    output.async_read(output_data.output, "stdout"sv));

				if (error.read > -1)
					threads.push_back(
					    error.async_read(output_data.error, "stderr"sv));

				for (auto& thread : threads) {
					thread.join();
				}

				std::string result;
				result.reserve(input.debug.size() + output.debug.size() +
				               error.debug.size());
				result.append(input.debug);
				result.append(output.debug);
				result.append(error.debug);
				return result;
			}
		};

		pid_t spawn(std::filesystem::path const& program_path,
		            args::arglist args,
		            std::map<std::string, std::string> const* env_ptr,
		            std::filesystem::path const* cwd,
		            pipes_type const& pipes,
		            std::string& debug) {
			std::vector<char*> argv;
			argv.reserve(2 + args.size());  // arg0 and NULL
			auto filename = program_path.filename();
			argv.push_back(const_cast<char*>(filename.c_str()));
			for (unsigned i = 0; i < args.size(); ++i)
				argv.push_back(const_cast<char*>(args[i].data()));
			argv.push_back(nullptr);

			std::vector<std::string> env_pairs{};
			std::vector<char*> environment{};

			if (env_ptr) {
				env_pairs.reserve(env_ptr->size());
				environment.reserve(env_ptr->size() + 1);

				for (auto const& [key, value] : *env_ptr) {
					env_pairs.push_back(fmt::format("{}={}", key, value));
				}

				for (auto& env_var : env_pairs) {
					environment.push_back(env_var.data());
				}

				environment.push_back(nullptr);
			}

#if defined(STDOUT_DUMP)
#define CHECK(X)                                         \
	do {                                                 \
		auto ret = X;                                    \
		debug.append(fmt::format("{} : {}\n", ret, #X)); \
	} while (0)
#else
#define CHECK(X) X
#endif

			posix_spawn_file_actions_t actions{};

			CHECK(posix_spawn_file_actions_init(&actions));
			std::unique_ptr<posix_spawn_file_actions_t,
			                decltype([](posix_spawn_file_actions_t* a) {
				                posix_spawn_file_actions_destroy(a);
			                })>
			    actions_anchor{&actions};

			if (cwd)
				CHECK(posix_spawn_file_actions_addchdir_np(&actions,
				                                           cwd->c_str()));

			/* Standard input */
			if (pipes.input.write != -1)
				CHECK(posix_spawn_file_actions_addclose(&actions,
				                                        pipes.input.write));
			if (pipes.input.read != -1) {
				CHECK(posix_spawn_file_actions_adddup2(&actions,
				                                       pipes.input.read, 0));
				CHECK(posix_spawn_file_actions_addclose(&actions,
				                                        pipes.input.read));
			}
			/* Standard output */
			if (pipes.output.read != -1)
				CHECK(posix_spawn_file_actions_addclose(&actions,
				                                        pipes.output.read));
			if (pipes.output.write != -1) {
				CHECK(posix_spawn_file_actions_adddup2(&actions,
				                                       pipes.output.write, 1));
				CHECK(posix_spawn_file_actions_addclose(&actions,
				                                        pipes.output.write));
			}
			/* Standard error output */
			if (pipes.error.read != -1)
				CHECK(posix_spawn_file_actions_addclose(&actions,
				                                        pipes.error.read));
			if (pipes.error.write != -1) {
				CHECK(posix_spawn_file_actions_adddup2(&actions,
				                                       pipes.error.write, 2));
				CHECK(posix_spawn_file_actions_addclose(&actions,
				                                        pipes.error.write));
			}

			posix_spawnattr_t attrs{};
			CHECK(posix_spawnattr_init(&attrs));
			std::unique_ptr<posix_spawnattr_t,
			                decltype([](posix_spawnattr_t* a) {
				                posix_spawnattr_destroy(a);
			                })>
			    attr_anchor{&attrs};

			CHECK(posix_spawnattr_setpgroup(&attrs, 0));

			pid_t result;
			CHECK(posix_spawn(
			    &result, program_path.c_str(), &actions, &attrs, argv.data(),
			    environment.empty() ? environ : environment.data()));
			return result;
		}
	}  // namespace

	capture run(run_opts const& options) {
		capture result{};

		auto const executable = where({}, "PATH", options.exec);
		if (executable.empty()) {
			result.return_code = -ENOENT;
			return result;
		}

		std::string debug;
		pipes_type pipes{};
		if (!pipes.open(options, debug)) {
			// GCOV_EXCL_START[POSIX]
			[[unlikely]];
			debug.append("pipes did not open\n");
			result.return_code = 128;
			return result;
		}  // GCOV_EXCL_STOP

		auto child = spawn(executable, options.args, options.env, options.cwd,
		                   pipes, debug);

		debug.append(pipes.io(options.input, result));

		int status;
		errno = 0;
		auto const ret_pid = waitpid(child, &status, 0);

#if defined(STDOUT_DUMP)
		auto const err = errno;

		char str_error[1024];
		str_error[sizeof(str_error) - 1] = 0;
		auto str_error_msg = strerror_r(err, str_error, sizeof(str_error) - 1);

		debug.append(fmt::format(
		    "status[{}/{}, {}: {}]: {:x}; exit: {}, {}; term: {}, {} (core: "
		    "{}); stop: {}, {}\n",
		    child, ret_pid, err, str_error_msg, status,
		    WIFEXITED(status) ? "yes"sv : "no"sv,
		    WIFEXITED(status) ? fmt::to_string(static_cast<int>(
		                            static_cast<char>(WEXITSTATUS(status))))
		                      : "-"sv,
		    WIFSIGNALED(status) ? "yes"sv : "no"sv,
		    WIFSIGNALED(status) ? fmt::to_string(WTERMSIG(status)) : "-"sv,
		    WIFSIGNALED(status) ? WCOREDUMP(status) ? "yes"sv : "no"sv : "-"sv,
		    WIFSTOPPED(status) ? "yes"sv : "no"sv,
		    WIFSTOPPED(status) ? fmt::to_string(WSTOPSIG(status)) : "-"sv));
		if (options.debug) *options.debug = std::move(debug);
#endif

#define I_EXIT_STATUS(status) \
	static_cast<int>(static_cast<char>(WEXITSTATUS(status)))
		auto const code = WIFEXITED(status)     ? I_EXIT_STATUS(status)
		                  : WIFSIGNALED(status) ? WTERMSIG(status)
		                  : WIFSTOPPED(status)  ? WSTOPSIG(status)
		                                        : 128;

		result.return_code = code;
		return result;
	}

	std::optional<std::filesystem::path> find_program(
	    std::span<std::string const> names,
	    std::filesystem::path const& hint) {
		for (auto const& name : names) {
			auto candidate = where(hint, "PATH", name);
			if (!candidate.empty()) return candidate;
		}
		return std::nullopt;
	}
}  // namespace io
