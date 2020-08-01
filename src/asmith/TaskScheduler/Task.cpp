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

namespace asmith {
	// TaskHandle

	TaskHandle::TaskHandle(Task& task, Scheduler& scheduler, Priority priority) :
		_task(task),
		_scheduler(scheduler),
		_priority(priority)
	{}

	TaskHandle::~TaskHandle() {
		_Wait();
	}

	void TaskHandle::_Wait() {
		if(_task._state == Task::STATE_INITIALISED || _task._state==Task::STATE_COMPLETE) return;

		_scheduler.Yield([this]()->bool {
			return _task._state == Task::STATE_COMPLETE;
		});
	}

	void TaskHandle::Wait() {
		if (_task._state == Task::STATE_INITIALISED) throw std::runtime_error("Task has not been scheduled");

		// Rethrow a caught exception
		if (_exception) {
			std::exception_ptr tmp = _exception;
			_exception = std::exception_ptr();
			std::rethrow_exception(tmp);
		}
		_Wait();
	}

	// Task

	Task::Task() :
		_state(STATE_INITIALISED)
	{}

	Task::~Task() {

	}

	void Task::Yield(const std::function<bool()>& condition) {
		TaskHandle* const handle = _handle.get();
		if (_state != STATE_EXECUTING) throw std::runtime_error("Task cannot yeild unless it is in STATE_EXECUTING");
		_state = STATE_BLOCKED;


#if ASMITH_TASK_CALLBACKS
		try {
			OnBlock();
		} catch (...) {
			_state = STATE_EXECUTING;
			_handle->_exception = std::current_exception();
		}
#endif

		try {
			handle->_scheduler.Yield(condition);
		} catch (...) {
			_state = STATE_EXECUTING;
			std::rethrow_exception(std::current_exception());
		}
		_state = STATE_EXECUTING;

#if ASMITH_TASK_CALLBACKS
		try {
			OnResume();
		} catch (...) {
			_handle->_exception = std::current_exception();
		}
#endif
	}

	// Scheduler

	Scheduler::Scheduler() {

	}

	Scheduler::~Scheduler() {
		//! \bug Scheduled tasks are left in an undefined state
	}

	bool Scheduler::ExecuteNextTask() throw() {
		Task* task = nullptr;

		{
			std::lock_guard<std::mutex> lock(_mutex);
			if (_task_queue.empty()) return false;
			task = _task_queue.back();
			_task_queue.pop_back();
		}

		try {
			task->_state = Task::STATE_EXECUTING;
			task->Execute();
			task->_state = Task::STATE_COMPLETE;
		} catch (...) {
			task->_handle->_exception = std::current_exception();
			task->_state = Task::STATE_COMPLETE;
		}

		// Wake waiting threads
		_task_queue_update.notify_all();

		return true;
	}

	void Scheduler::Yield(const std::function<bool()>& condition) {
		// While the condition is not met
		while (!condition()) {
			// Try to execute a scheduled task
			if (!ExecuteNextTask()) {
				// If no task was scheduled then block until an update
				std::unique_lock<std::mutex> lock(_mutex);
				_task_queue_update.wait(lock);
			}
		}
	}

	std::shared_ptr<TaskHandle> Scheduler::Schedule(Task& task, Priority priority) {
		// Initial error checking
		if (task._state != Task::STATE_INITIALISED) throw std::runtime_error("Task cannot be scheduled unless it is in STATE_INITIALISED");

		// State change
		task._state = Task::STATE_INITIALISED;

		// Create handle
		std::shared_ptr<TaskHandle> handle(new TaskHandle(task, *this, priority));
		task._handle = handle;

#if ASMITH_TASK_CALLBACKS
		// Task callback
		try {
			task.OnScheduled();
		} catch (...) {
			task._handle->_exception = std::current_exception();
			task._state = Task::STATE_COMPLETE;
			return handle;
		}
#endif

		// Add to task queue
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_task_queue.push_back(&task);

			// Sort task list by priority
			std::sort(_task_queue.begin(), _task_queue.end(), [](const Task* const lhs, const Task* const rhs)->bool {
				return lhs->_handle->_priority < rhs->_handle->_priority;
			});
		}

		// Notify waiting threads
		_task_queue_update.notify_all();


		return handle;
	}

}