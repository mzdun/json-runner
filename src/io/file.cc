// Copyright (c) 2022 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "io/file.hh"
#include <algorithm>

namespace io {
	using std::filesystem::path;

	file::file() = default;
	file::~file() = default;
	file::file(file&&) = default;
	file& file::operator=(file&&) = default;

	FILE* file::fopen(path file, char const* mode) noexcept {
		file.make_preferred();
#if defined WIN32 || defined _WIN32
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
		std::unique_ptr<wchar_t[]> heap;
		// 10 characters is an overkill, this will std::abort on a first ASSERT
		// inside libc...
		wchar_t buff[10];
		wchar_t* dst = buff;
		auto len = std::min(strlen(mode), size_t{9u});
		buff[len] = 0;

		while (len--)
			*dst++ = *mode++;

		return ::_wfopen(file.native().c_str(), buff);
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#else  // WIN32 || _WIN32
		return std::fopen(file.string().c_str(), mode);
#endif
	}

	std::vector<std::byte> file::read() const {
		std::vector<std::byte> out;
		if (!*this) return out;
		std::byte buffer[1024];

		while (true) {
			auto ret = std::fread(buffer, 1, sizeof(buffer), get());
			if (!ret) {
				if (!std::feof(get())) out.clear();
				break;
			}
			out.insert(end(out), buffer, buffer + ret);
		}

		return out;
	}

	constexpr long to_long(long value) noexcept { return value; }
	constexpr long to_long(auto value) noexcept {
		return static_cast<long>(value);
	}

	std::string file::read_line() const {
		std::string out;
		if (!*this) return out;

		char buffer[1024];

		while (true) {
			auto ret = std::fread(buffer, 1, sizeof(buffer), get());
			if (!ret) {
				// GCOV_EXCL_START
				if (!std::feof(get())) out.clear();
				break;
				// GCOV_EXCL_STOP
			}
			auto buf_end = buffer + ret;
			auto it = std::find(buffer, buf_end, '\n');
			if (it == buf_end) {
				out.insert(end(out), buffer, buf_end);
				continue;
			}

			auto new_length = static_cast<size_t>(it - buffer);
			out.insert(end(out), buffer, buffer + new_length);
			auto rewind =
			    static_cast<std::make_signed_t<size_t>>(ret - new_length) - 1;
			if (rewind > 0) std::fseek(get(), to_long(-rewind), SEEK_CUR);
			break;
		}  // GCOV_EXCL_LINE[WIN32]

		return out;
	}

	size_t file::load(void* buffer, size_t length) const noexcept {
		return std::fread(buffer, 1, length, get());
	}

	size_t file::store(const void* buffer, size_t length) const noexcept {
		if (!length) return 0;
		return std::fwrite(buffer, 1, length, get());
	}

	bool file::skip(size_t length) const noexcept {
		while (length) {
			constexpr auto max_int =
			    static_cast<size_t>(std::numeric_limits<int>::max());
			auto const chunk = static_cast<int>(std::min(max_int, length));
			if (std::fseek(get(), chunk, SEEK_CUR)) return false;
			length -= static_cast<size_t>(chunk);
		}
		return true;
	}

	file fopen(const path& file, char const* mode) noexcept {
		return {file, mode};
	}
}  // namespace io
