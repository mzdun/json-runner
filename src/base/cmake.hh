// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <string>
#include <vector>

namespace cmake {
	struct project {
		std::string name{};
		std::string version{};
		std::string stability{};
		std::string description{};

		std::string ver() const { return version + stability; }
		std::string pkg() const { return name + "-" + ver(); }
		std::string tag() const { return "v" + ver(); }
	};

	project const& get_project();
}
