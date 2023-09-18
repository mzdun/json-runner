// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace testbed {
	struct handler_info;
	struct runtime;
}  // namespace testbed

class Chai {
public:
	Chai();
	~Chai();
	Chai(Chai const&) = delete;
	Chai& operator=(Chai const&) = delete;
	Chai(Chai&&) = delete;
	Chai& operator=(Chai&&) = delete;

	struct ProjectInfo {
		std::string target;
		std::vector<std::string> allowed;
		std::vector<std::string> install_components;
		std::string datasets_dir;
		std::optional<std::string> default_dataset;
		std::map<std::string, std::string> environment;
		std::map<std::string, std::string> common_patches;
		std::map<std::string, testbed::handler_info> script_handlers;
		std::function<void(std::string const&, testbed::runtime const&)>
		    installer;

		std::map<std::string, testbed::handler_info> handlers() const;
	};

	ProjectInfo const& project() noexcept;

private:
	struct Impl;
	std::unique_ptr<Impl> pimpl;
};
