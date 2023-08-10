// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "io/run.hh"
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

		static pid_t pid = -1;

		// GCOV_EXCL_START[POSIX]
		void forward_signal(int signo) { kill(pid, signo); }
		// GCOV_EXCL_STOP

		struct pipe_type {
			int read{-1};
			int write{-1};

			~pipe_type() {
				close_side(read);
				close_side(write);
			}

			void close_read() { close_side(read); }
			void close_write() { close_side(write); }

			void dup_read(int stdio) {
				close_write();
				if (read == -1) return;
				::close(stdio);
				::dup2(read, stdio);
				close_read();
			}

			void dup_write(int stdio) {
				close_read();
				if (write == -1) return;
				::close(stdio);
				::dup2(write, stdio);
				close_write();
			}

			bool open() {
				int fd[2];
				if (::pipe(fd) == -1) return false;
				read = fd[0];
				write = fd[1];
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
			static void dump(std::span<char const> buffer) {
				static constexpr auto len = 20zu;
				char line[len * 4 + 2];
				line[len * 4] = '\n';
				line[len * 4 + 1] = 0;
				auto index = 0zu;
				for (auto b : buffer) {
					if (index == len) {
						fputs(line, stdout);
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
					fputs(line, stdout);
				}
			}
#endif

			std::thread async_read(std::string& dst, std::string_view name) {
				return std::thread(
				    [name](int fd, std::string& bytes) {
					    char buffer[BUFSIZE];

					    while (true) {
						    auto const actual =
						        ::read(fd, buffer, std::size(buffer));
						    if (actual <= 0) break;
#ifdef STDOUT_DUMP
						    fmt::print("> {}:\n", name);
						    dump({buffer, buffer + actual});
#endif
						    bytes.insert(bytes.end(), buffer, buffer + actual);
					    }
				    },
				    read, std::ref(dst));
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

			bool open(pipe directions) {
#define OPEN(DIRECTION)                                    \
	if ((directions & pipe::DIRECTION) == pipe::DIRECTION) \
		if (!DIRECTION.open()) {                           \
			[[unlikely]];                                  \
			return false;                                  \
		}

				OPEN(input);
				OPEN(output);
				OPEN(error);
				return true;
			}

			void io(std::string_view input_data,
			        capture& output_data,
			        pipe directions) {
				input.close_read();
				output.close_write();
				error.close_write();

				std::vector<std::thread> threads{};
				threads.reserve(
				    ((directions & pipe::input) == pipe::input ? 1u : 0u) +
				    ((directions & pipe::output) == pipe::output ? 1u : 0u) +
				    ((directions & pipe::error) == pipe::error ? 1u : 0u));

				if ((directions & pipe::input) == pipe::input)
					threads.push_back(input.async_write(input_data));

				if ((directions & pipe::output) == pipe::output)
					threads.push_back(
					    output.async_read(output_data.output, "stdout"sv));

				if ((directions & pipe::error) == pipe::error)
					threads.push_back(
					    error.async_read(output_data.error, "stderr"sv));

				for (auto& thread : threads) {
					thread.join();
				}
			}

			void dup() {
				input.dup_read(0);
				output.dup_write(1);
				error.dup_write(2);
			}
		};

		void spawn(std::filesystem::path const& program_path,
		           args::arglist args,
		           std::map<std::string, std::string> const* env_ptr,
		           std::filesystem::path const* cwd,
		           pipes_type const& pipes) {
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

#define CHECK(X)                          \
	/*do {                                \
	    auto ret = X;                     \
	    fmt::print("{} : {}\n", ret, #X); \
	} while (0)*/                         \
	X
			posix_spawn_file_actions_t actions{};
			CHECK(posix_spawn_file_actions_init(&actions));
			std::unique_ptr<posix_spawn_file_actions_t,
			                decltype([](posix_spawn_file_actions_t* a) {
				                posix_spawn_file_actions_destroy(a);
			                })>
			    anchor{&actions};

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

			// fmt::print("actions: [size:{} capacity:{}]\n", actions.__used,
			//            actions.__allocated);

			CHECK(posix_spawn(
			    &pid, program_path.c_str(), &actions, nullptr, argv.data(),
			    environment.empty() ? environ : environment.data()));
		}
	}  // namespace

	capture run(run_opts const& options) {
		capture result{};

		auto const executable = where({}, "PATH", options.exec);
		if (executable.empty()) {
			result.return_code = -ENOENT;
			return result;
		}

		pipes_type pipes{};
		if (!pipes.open(options.pipe)) {
			// GCOV_EXCL_START[POSIX]
			[[unlikely]];
			result.return_code = 128;
			return result;
		}  // GCOV_EXCL_STOP

		spawn(executable, options.args, options.env, options.cwd, pipes);

		signal(SIGINT, forward_signal);
		signal(SIGTERM, forward_signal);
#if defined(SIGQUIT)
		signal(SIGQUIT, forward_signal);
#endif  // defined(SIGQUIT)

		pipes.io(options.input, result, options.pipe);

		int status;
		waitpid(pid, &status, 0);
		if (WIFEXITED(status)) {
			result.return_code = static_cast<char>(WEXITSTATUS(status));
			return result;
		}
		// GCOV_EXCL_START[POSIX]
		[[unlikely]];
		result.return_code = (WIFSIGNALED(status)) ? WIFSIGNALED(status) : 128;
		return result;
		// GCOV_EXCL_STOP
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
