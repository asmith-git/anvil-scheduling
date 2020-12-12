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

#include <algorithm>
#include "asmith/TaskScheduler/Core.hpp"
#include "asmith/TaskScheduler/Scheduler.hpp"
#include "asmith/TaskScheduler/Task.hpp"
#include "asmith/TaskScheduler/MultiTask.hpp"

namespace asmith {

	// Task

	Task::Task() :
		_scheduler(nullptr),
		_priority(PRIORITY_MIDDLE),
		_state(STATE_INITIALISED)
	{}

	Task::~Task() {
		//! \bug If the task is scheduled it must be removed from the scheduler
	}

	void Task::Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds) {
		if (_state != STATE_EXECUTING) throw std::runtime_error("Task cannot yeild unless it is in STATE_EXECUTING");

		if (condition()) return;

#if ASMITH_TASK_CALLBACKS
		try {
			OnBlock();
		} catch (...) {
			std::rethrow_exception(std::current_exception());
		}
#endif

		_state = STATE_BLOCKED;
		try {
			_scheduler->Yield(condition, max_sleep_milliseconds);
		} catch (...) {
			_state = STATE_EXECUTING;
			std::rethrow_exception(std::current_exception());
		}
		_state = STATE_EXECUTING;

#if ASMITH_TASK_CALLBACKS
		OnResume();
#endif
	}

	void Task::Wait() {
		Scheduler* scheduler = _scheduler;
		if (scheduler == nullptr || _state == Task::STATE_COMPLETE) return;

		scheduler->Yield([this]()->bool {
			return _state == Task::STATE_COMPLETE;
		});

		// Rethrow a caught exception
		if (_exception) {
			std::exception_ptr tmp = _exception;
			_exception = std::exception_ptr();
			std::rethrow_exception(tmp);
		}
	}


	void Task::SetPriority(const Priority priority) {
		Scheduler* scheduler = _scheduler;
		if (scheduler) {
			std::lock_guard<std::mutex> lock(scheduler->_mutex);
			if (_state == STATE_SCHEDULED) {
				_priority = priority;
				scheduler->SortTaskQueue();
			} else {
				throw std::runtime_error("Priority of a task cannot be changed when executing");
			}
		} else {
			_priority = priority;
		}
	}

	// Scheduler

	Scheduler::Scheduler() {

	}

	Scheduler::~Scheduler() {
		//! \bug Scheduled tasks are left in an undefined state
	}

	bool Scheduler::TryToExecuteTask() throw() {
		Task* task = nullptr;

		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_task_queue.empty()) return false;
			task = _task_queue.back();
			_task_queue.pop_back();
		}

		task->_state = Task::STATE_EXECUTING;
		try {
			task->Execute();
		} catch (...) {
			task->_exception = std::current_exception();
		}
		task->_state = Task::STATE_COMPLETE;
		task->_scheduler = nullptr;

		// Wake waiting threads
		_task_queue_update.notify_all();

		return true;
	}

	void Scheduler::Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds) {
		max_sleep_milliseconds = std::max(1u, max_sleep_milliseconds);

		// While the condition is not met
		while (!condition()) {
			// Try to execute a scheduled task
			if (! TryToExecuteTask()) {
				// If no task was scheduled then block until an update
				std::unique_lock<std::mutex> lock(_mutex);
				if (max_sleep_milliseconds == UINT32_MAX) { // Special behaviour, only wake when task updates happen (useful for implementing a thread pool)
					_task_queue_update.wait(lock);
				}else {
					_task_queue_update.wait_for(lock, std::chrono::milliseconds(max_sleep_milliseconds));
				}
			}
		}
	}

	void Scheduler::SortTaskQueue() throw() {
		std::sort(_task_queue.begin(), _task_queue.end(), [](const Task* const lhs, const Task* const rhs)->bool {
			return lhs->_priority < rhs->_priority;
		});
	}

	void Scheduler::Schedule(Task** tasks, const uint32_t count) {
		// Initial error checking
		for (uint32_t i = 0u; i < count; ++i) {
			if (tasks[i]->_state != Task::STATE_INITIALISED) throw std::runtime_error("Task cannot be scheduled unless it is in STATE_INITIALISED");
		}

		for (uint32_t i = 0u; i < count; ++i) {
			// State change
			Task& t = *tasks[i];
			t._state = Task::STATE_SCHEDULED;
			t._exception = std::exception_ptr();

#if ASMITH_TASK_CALLBACKS
			// Task callback
			try {
				t.OnScheduled();
			} catch (...) {
				t._exception = std::current_exception();
				t._state = Task::STATE_COMPLETE;
			}
#endif
		}

		// Add to task queue
		{
			std::lock_guard<std::mutex> lock(_mutex);
			for (uint32_t i = 0u; i < count; ++i) {
#if ASMITH_TASK_CALLBACKS
				if(tasks[i]->_state == Task::STATE_SCHEDULED)
#endif
				_task_queue.push_back(tasks[i]);
			}

			// Sort task list by priority
			SortTaskQueue();
		}

		// Notify waiting threads
		_task_queue_update.notify_all();
	}
	
	void Scheduler::Schedule(Task& task, Priority priority) {
		task.SetPriority(priority);
		Schedule(task);
	}
}