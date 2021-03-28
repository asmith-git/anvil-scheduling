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

#ifndef ANVIL_SCHEDULER_SCHEDULER_HPP
#define ANVIL_SCHEDULER_SCHEDULER_HPP

#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>
#include <memory>
#include "asmith/TaskScheduler/Core.hpp"

namespace anvil {

	class Scheduler {
	private:
		Scheduler(Scheduler&&) = delete;
		Scheduler(const Scheduler&) = delete;
		Scheduler& operator=(Scheduler&&) = delete;
		Scheduler& operator=(const Scheduler&) = delete;

#if ANVIL_TASK_DELAY_SCHEDULING
		std::vector<Task*> _unready_task_queue; //!< Contains tasks that have been scheduled but are not yet ready to execute
#endif
		std::vector<Task*> _task_queue;			//!< Contains tasks that have been scheduled and are ready to execute
		void SortTaskQueue() throw();

		Task* RemoveNextTaskFromQueue() throw();

		/*!
			\brief Called when a Task has been added or removed from the queue
			\details Wakes up threads that were sleeping and performs some additional scheduling logic
		*/
		void TaskQueueNotify();


#if ANVIL_TASK_DELAY_SCHEDULING
		void CheckUnreadyTasks();
#endif
	protected:
		std::condition_variable _task_queue_update;
		std::mutex _mutex;

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

		void Schedule(Task& task, Priority priority);
	};
}

#endif