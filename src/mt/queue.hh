// Copyright (c) 2023 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <stop_token>
#include <utility>

namespace mt {
	template <typename Element>
	class mt_queue {
	public:
		mt_queue() = default;
		mt_queue(mt_queue const& other) {
			std::lock_guard lock{other.m_};
			items_ = other.items_;
		}
		mt_queue& operator=(mt_queue const& other) {
			std::unique_lock self{m_, std::defer_lock};
			std::unique_lock lock{other.m_, std::defer_lock};
			std::lock(self, lock);
			items_ = other.items_;
			return *this;
		}

		bool empty() const {
			std::lock_guard lock{m_};
			return items_.empty();
		}

		size_t size() const {
			std::lock_guard lock{m_};
			return items_.size();
		}

		void wake() { cv_.notify_all(); }

		void push(Element const& value) {
			std::lock_guard lock{m_};
			items_.push(value);
			++total_;
			cv_.notify_one();
		}
		void push(Element&& value) {
			std::lock_guard lock{m_};
			items_.push(std::move(value));
			++total_;
			cv_.notify_one();
		}

		bool wait_and_pop(Element& result, std::stop_token tok) {
			std::unique_lock lock{m_};
			cv_.wait(lock, [this, &tok] {
				return !items_.empty() || tok.stop_requested();
			});
			if (!items_.empty()) {
				result = std::move(items_.front());
				items_.pop();
				++popped_;
			}
			return !tok.stop_requested();
		}

	private:
		mutable std::mutex m_{};
		std::condition_variable cv_{};
		size_t total_{};
		size_t popped_{};
		std::queue<Element> items_{};
	};
}  // namespace mt
