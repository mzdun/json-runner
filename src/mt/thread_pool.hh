// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <filesystem>
#include <functional>
#include <future>
#include <optional>
#include <thread>
#include <vector>
#include "mt/queue.hh"

namespace fs = std::filesystem;

enum class outcome { OK, SKIPPED, SAVED, FAILED, CLIP_FAILED };

struct test_results {
	outcome result;
	std::string task_ident;
	fs::path temp_dir;
	std::string prepare;
	std::optional<std::string> report{std::nullopt};
};

namespace mt {
	class thread_pool {
	public:
		thread_pool(size_t size = std::thread::hardware_concurrency());
		~thread_pool();
		void push(std::packaged_task<test_results()>&& task);

	private:
		static void thread_proc(
		    std::stop_token tok,
		    mt::mt_queue<std::packaged_task<test_results()>>& tasks);

		mt_queue<std::packaged_task<test_results()>> tasks_{};
		std::vector<std::jthread> threads_{};
	};
}  // namespace mt
