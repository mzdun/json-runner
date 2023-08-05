// Copyright (c) 2022 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace io {
	struct fcloser {
		void operator()(FILE* f) { std::fclose(f); }
	};
	class file : private std::unique_ptr<FILE, fcloser> {
		using parent_t = std::unique_ptr<FILE, fcloser>;
		static FILE* fopen(std::filesystem::path fname,
		                   char const* mode) noexcept;

	public:
		file();
		~file();
		file(file const&) = delete;
		file& operator=(file const&) = delete;
		file(file&&);
		file& operator=(file&&);

		explicit file(std::filesystem::path const& fname) noexcept
		    : file(fname, "r") {}
		file(std::filesystem::path const& fname, char const* mode) noexcept
		    : parent_t(fopen(fname, mode)) {}

		using parent_t::operator bool;

		void close() noexcept { reset(); }
		void open(std::filesystem::path const& fname,
		          char const* mode = "r") noexcept {
			reset(fopen(fname, mode));
		}

		std::vector<std::byte> read() const;
		std::string read_line() const;
		size_t load(void* buffer, size_t length) const noexcept;
		size_t store(void const* buffer, size_t length) const noexcept;
		bool skip(size_t length) const noexcept;
		bool feof() const noexcept { return std::feof(get()); }
	};

	file fopen(std::filesystem::path const& file,
	           char const* mode = "r") noexcept;
}  // namespace io
