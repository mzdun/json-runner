// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "base/diff.hh"
#include <fmt/format.h>
#include <queue>
#include <span>
#include <vector>
#include "base/str.hh"

using namespace std::literals;

struct op {
	bool remove;
	size_t index;
};

struct changeset {
	size_t after;
	size_t before;
	std::vector<op> change{};

	changeset remove() {
		auto copy = *this;
		copy.change.push_back({.remove = true, .index = before});
		++copy.before;
		return copy;
	}

	changeset add() {
		auto copy = *this;
		copy.change.push_back({.remove = false, .index = after});
		++copy.after;
		return copy;
	}

	size_t offset(std::span<std::string_view const> after) const noexcept {
		return (after.size() + 1) * before + this->after;
	}
	size_t value() const noexcept { return change.size(); }

	std::pair<std::string_view, std::string_view> intersection(
	    std::span<std::string_view const> after,
	    std::span<std::string_view const> before) const noexcept {
		return {at(this->after, after), at(this->before, before)};
	}

	static std::string_view at(
	    size_t index,
	    std::span<std::string_view const> lines) noexcept {
		return index < lines.size() ? lines[index] : ""sv;
	}
};

using generation = std::vector<changeset>;

changeset find_route(std::span<std::string_view const> before,
                     std::span<std::string_view const> after) {
	std::vector<size_t> matrix((before.size() + 1) * (after.size() + 1),
	                           std::numeric_limits<size_t>::max());
	std::queue<generation> generations{};
	generations.push({{.after = 0, .before = 0}});

	changeset solution{0, 0};
	while (!generations.empty()) {
		auto gen = generations.front();
		generations.pop();

		std::vector<changeset> next_gen{};

		bool found_corner = false;
		for (auto step : gen) {
			auto const step_value = step.value();
			auto step_offset = step.offset(after);

			while (step.after < after.size() && step.after < before.size() &&
			       step_offset < matrix.size() &&
			       matrix[step_offset] > step_value) {
				auto pair = step.intersection(after, before);
				if (pair.first != pair.second) break;
				matrix[step_offset] = step_value;
				++step.after;
				++step.before;
				step_offset = step.offset(after);
			}

			if (step.after == after.size() && step.before == before.size()) {
				if (matrix[step_offset] > step_value) {
					matrix[step_offset] = step_value;
					solution = std::move(step);
					found_corner = true;
					break;
				}
				continue;
			}

			if (step.after >= (after.size() + 1) ||
			    step.before >= (before.size() + 1)) {
				// overboard
				continue;
			}

			if (matrix[step_offset] <= step_value) {
				// too slow
				continue;
			}

			matrix[step.offset(after)] = step_value;
			next_gen.push_back(step.remove());
			next_gen.push_back(step.add());
		}

		if (found_corner) break;
		if (next_gen.empty()) continue;
		generations.push(std::move(next_gen));
	}

	return solution;
}

std::string diff(std::string_view text_before, std::string_view text_after) {
	auto enter_before = last_enter(text_before);
	auto enter_after = last_enter(text_after);
	auto before = split(enter_before, '\n');
	auto after = split(enter_after, '\n');
	auto operations = find_route(before, after);

	auto const resulting_lines =
	    before.size() +
	    std::ranges::count_if(operations.change,
	                          [](auto const& op) { return !op.remove; }) -
	    std::ranges::count_if(operations.change,
	                          [](auto const& op) { return op.remove; });
	std::vector<std::string> lines{};
	lines.reserve(resulting_lines);

	size_t before_index = 0;
	size_t after_index = 0;

	for (auto pair : operations.change) {
		auto& index = pair.remove ? before_index : after_index;
		auto const& src = pair.remove ? before : after;
		if (pair.index > index) {
			for (; index < pair.index; ++before_index, ++after_index) {
				lines.push_back(fmt::format(" {}", before[before_index]));
			}
		}
		index = pair.index + 1;
		lines.push_back(
		    fmt::format("{}{}", pair.remove ? '-' : '+', src[pair.index]));
	}

	auto const before_size = before.size();
	for (; before_index < before_size; ++before_index)
		lines.push_back(fmt::format(" {}", before[before_index]));

	return fmt::format("{}", fmt::join(lines, "\n"));
}

#if 0
	static constexpr auto A = R"(This part of the
document has stayed the
same from version to
version.  It shouldn't
be shown if it doesn't
change.  Otherwise, that
would not be helping to
compress the size of the
changes.

This paragraph contains
text that is outdated.
It will be deleted in the
near future.

It is important to spell
check this dokument. On
the other hands, a
misspelled word isn't
the end of the world.
Nothing in the rest of
this paragraph needs to
be changed. Things can
be added after it.
)"sv;
	static constexpr auto B = R"(This is an important
notice! It should
therefore be located at
the beginning of this
document!

This part of the
document has stayed the
same from version to
version.  It shouldn't
be shown if it doesn't
change.  Otherwise, that
would not be helping to
compress the size of the
changes.

It is important to spell
check this document. On
the other hand, a
misspelled word isn't
the end of the world.
Nothing in the rest of
this paragraph needs to
be changed. Things can
be added after it.

This paragraph contains
important new additions
to this document.
)"sv;

	static constexpr auto C =
	    "[detached HEAD $REPORT] reported files\n 2 files,  86% (6/7, -1)\n based on $HEAD@main\n parent $PARENT\n contains $BUILD:  86%\n"sv;
	static constexpr auto D =
	    "[detached HEAD 1a6b5f636] reported files\n 2 files,  86% (6/7, -1)\n based on 0068bfcf0@main\n parent dd6f314e6\n contains 350755bb7:  86%\n"sv;

	fmt::print("{}\n", diff(A, B));
	return 0;
#endif
