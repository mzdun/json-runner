// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <random>

template <typename RandomDevice = std::random_device>
struct basic_seed_sequence {
	RandomDevice rd{};

	using result_type = typename RandomDevice::result_type;

	template <typename RandomAccessIterator>
	void generate(RandomAccessIterator begin, RandomAccessIterator end) {
		using value_type =
		    typename std::iterator_traits<RandomAccessIterator>::value_type;
		std::uniform_int_distribution<value_type> bits{
		    (std::numeric_limits<value_type>::min)(),
		    (std::numeric_limits<value_type>::max)(),
		};

		for (auto it = begin; it != end; ++it) {
			*it = bits(rd);
		}
	}

	static std::mt19937 mt19937() {
		basic_seed_sequence<RandomDevice> seq{};
		return std::mt19937{seq};
	}
};

using seed_sequence = basic_seed_sequence<>;
