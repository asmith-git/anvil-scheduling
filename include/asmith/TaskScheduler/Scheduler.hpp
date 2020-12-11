//MIT License
//
//Copyright(c) 2020 Adam G. Smith
//
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files(the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following conditions :
//
//The above copyright notice and this permission notice shall be included in all
//copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//SOFTWARE.

#ifndef ASMITH_SCHEDULER_SCHEDULER_HPP
#define ASMITH_SCHEDULER_SCHEDULER_HPP

#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>
#include <memory>
#include "asmith/TaskScheduler/Core.hpp"

namespace asmith {

	class Scheduler {
	private:
	private:
		Scheduler(Scheduler&&) = delete;
		Scheduler(const Scheduler&) = delete;
		Scheduler& operator=(Scheduler&&) = delete;
		Scheduler& operator=(const Scheduler&) = delete;

		std::vector<Task*> _task_queue;
	protected:
		std::mutex _mutex;
		std::condition_variable _task_queue_update;

		bool TryToExecuteTask() throw();
	public:
		friend Task;
		typedef uint8_t Priority;

		Scheduler();
		virtual ~Scheduler();

		void Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds = 33u);

		void Schedule(Task** tasks, const uint32_t count);

		inline void Schedule(Task& task) {
			Task* t = &task;
			Schedule(&t, 1u);
		}

		inline void Schedule(Task& task, Priority priority) {
			task.SetPriority(priority);
			Schedule(task);
		}
	};
}

#endif