// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#include "mt/thread_pool.hh"

namespace mt {
	thread_pool::thread_pool(size_t size) {
		if (!size) size = 1;
		threads_.reserve(size);
		for (size_t index = 0; index < size; ++index)
			threads_.push_back(std::jthread{thread_proc, std::ref(tasks_)});
	}

	thread_pool::~thread_pool() {
		for (auto& thread : threads_)
			thread.request_stop();
		tasks_.wake();
	}

	void thread_pool::push(std::packaged_task<test_results()>&& task) {
		tasks_.push(std::move(task));
	}

	void thread_pool::thread_proc(
	    std::stop_token tok,
	    mt::mt_queue<std::packaged_task<test_results()>>& tasks) {
		while (!tok.stop_requested()) {
			std::packaged_task<test_results()> task;
			if (tasks.wait_and_pop(task, tok)) {
				task();
			}
		}
	}
}  // namespace mt
