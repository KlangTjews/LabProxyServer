#pragma once

#include <iostream>
#include <queue>
#include <vector>
#include <thread>
#include <atomic>
#include <future>
#include <mutex>
#include <condition_variable>
#include "Singleton.h"

class ThreadPool : public Singleton<ThreadPool> {
	friend class Singleton;
public:
	~ThreadPool() {
		Stop();
	};

	int idleThreadCount() {
		return thread_num_;
	}

	template <typename F, typename... Args>
	auto commit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
		using ReturnType = typename std::invoke_result<F, Args...>::type;
		auto task = std::make_shared<std::packaged_task<ReturnType()>>([f = std::forward<F>(f), ... args = std::forward<Args>(args)]() mutable {
			return f(args...);
		});

		auto future = task->get_future();
		{
			std::lock_guard<std::mutex> lock(cv_mt_);
			if (stop_.load())
				throw std::runtime_error("ThreadPool had stopped, can't commit new tasks");
			tasks_.emplace([task]() { (*task)(); });
		}
		cv_lock_.notify_one();
		return future;
	}

	using Task = std::packaged_task<void()>;

private:
	ThreadPool(unsigned int num = std::thread::hardware_concurrency()) : stop_(false) {
		if (num <= 1) {
			thread_num_ = 2;
		}
		else {
			thread_num_ = num;
		}

		Start();
	};

	void Start() {
		for (int i = 0; i < thread_num_; ++i) {
			pool_.emplace_back([this]() {
				while (!this->stop_.load()) {
					Task task;
					{
						std::unique_lock<std::mutex> cv_mt(cv_mt_);
						this->cv_lock_.wait(cv_mt, [this]() {
							return (this->stop_.load() || !this->tasks_.empty());
							});

						if (this->tasks_.empty()) {
							return;
						}

						task = std::move(this->tasks_.front());
						this->tasks_.pop();
					}
					this->thread_num_--;
					task();
					this->thread_num_++;
				}
				});
		}
	}

	void Stop() {
		stop_.store(true);
		cv_lock_.notify_all();

		for (auto& td : pool_) {
			if (td.joinable()) {
				std::cout << "Join thread " << td.get_id() << std::endl;
				td.join();
			}
		}
	}

	std::atomic_int thread_num_;
	std::queue<Task> tasks_;
	std::vector<std::thread> pool_;
	std::atomic_bool stop_;
	std::mutex cv_mt_;
	std::condition_variable cv_lock_;
};