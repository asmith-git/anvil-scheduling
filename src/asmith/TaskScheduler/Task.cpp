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

	// TaskHandle

	TaskHandle::TaskHandle() {

	}

	TaskHandle::~TaskHandle() {

	}

	namespace detail {
		// UniqueTaskHandle

		UniqueTaskHandle::UniqueTaskHandle(Task& task, Scheduler& scheduler, Priority priority) :
			_exception(nullptr),
			_task(task),
			_scheduler(scheduler),
			_priority(priority)
		{}

		UniqueTaskHandle::~UniqueTaskHandle() {
			_Wait();
		}

		void UniqueTaskHandle::_Wait() {
			if (_task._state == Task::STATE_INITIALISED || _task._state == Task::STATE_COMPLETE) return;

			_scheduler.Yield([this]()->bool {
				return _task._state == Task::STATE_COMPLETE;
			});

			_task._handle = nullptr;
		}

		void UniqueTaskHandle::Wait() {
			if (_task._state == Task::STATE_INITIALISED) throw std::runtime_error("Task has not been scheduled");

			_Wait();

			// Rethrow a caught exception
			if (_exception) {
				std::exception_ptr tmp = _exception;
				_exception = std::exception_ptr();
				std::rethrow_exception(tmp);
			}
		}
	}

	// Task

	Task::Task() :
		_state(STATE_INITIALISED)
	{}

	Task::~Task() {

	}

	void Task::Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds) {
		detail::UniqueTaskHandle* const handle = _handle;
		if (_state != STATE_EXECUTING) throw std::runtime_error("Task cannot yeild unless it is in STATE_EXECUTING");

		if (condition()) return;

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
			handle->_scheduler.Yield(condition, max_sleep_milliseconds);
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

	bool Scheduler::TryToExecuteTask() throw() {
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

	std::shared_ptr<TaskHandle> Scheduler::Schedule(Task& task, Priority priority) {
		// Initial error checking
		if (task._state != Task::STATE_INITIALISED) throw std::runtime_error("Task cannot be scheduled unless it is in STATE_INITIALISED");

		// State change
		task._state = Task::STATE_SCHEDULED;

		// Create handle
		std::shared_ptr<detail::UniqueTaskHandle> handle(new detail::UniqueTaskHandle(task, *this, priority));
		task._handle = handle.get();

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

	std::shared_ptr<TaskHandle> Scheduler::Schedule(MultiTask& task, Priority priority, const uint32_t count) {
		task._count = count;

		// Create handle
		std::shared_ptr<MultiTask::Handle> handle(new MultiTask::Handle(task, count, *this, priority));

		// Add to task queue
		{
			std::lock_guard<std::mutex> lock(_mutex);
			for (uint32_t i = 0u; i < task._count; ++i) {
				MultiTask::SubTask& subtask = handle->_subtasks[i];
				subtask._state = Task::STATE_SCHEDULED;
				subtask._handle = handle->_handles + i;
				_task_queue.push_back(&subtask);
			}

			// Sort task list by priority
			std::sort(_task_queue.begin(), _task_queue.end(), [](const Task* const lhs, const Task* const rhs)->bool {
				return lhs->_handle->_priority < rhs->_handle->_priority;
			});
		}

		// Notify waiting threads
		_task_queue_update.notify_all();


		return handle;
	}

	// MultiTask

	MultiTask::MultiTask() {

	}

	MultiTask::~MultiTask() {

	}

	// MultiTask::SubTask

	MultiTask::SubTask::SubTask(MultiTask& parent, const uint32_t index) :
		_parent(parent),
		_index(index)
	{}

	MultiTask::SubTask::~SubTask() {

	}

	void MultiTask::SubTask::Execute() {
		_parent.Execute(_index);
	}

#if ASMITH_TASK_CALLBACKS
	void MultiTask::SubTask::OnScheduled() {
		_parent.OnScheduled(_index);
	}

	void MultiTask::SubTask::OnBlock() {
		_parent.OnBlock(_index);
	}

	void MultiTask::SubTask::OnResume() {
		_parent.OnResume(_index);
	}
#endif

	// MultiTask::Handle

	MultiTask::Handle::Handle(MultiTask& parent, const uint32_t count, Scheduler& scheduler, Task::Priority priority) :
		_subtasks(nullptr),
		_handles(nullptr),
		_count(count)
	{
		// Use only one memory allocation for all tasks and handles to save on some overheads
		_subtasks = static_cast<MultiTask::SubTask*>(operator new((sizeof(MultiTask::SubTask) + sizeof(detail::UniqueTaskHandle)) * count));
		_handles = reinterpret_cast<detail::UniqueTaskHandle*>(_subtasks + count);

		for (uint32_t i = 0u; i < count; ++i) {
			new(_subtasks + i) MultiTask::SubTask(parent, i);
			new(_handles + i) detail::UniqueTaskHandle(_subtasks[i], scheduler, priority);
		}
	}

	MultiTask::Handle::~Handle() {
		_Wait();
	}

	void MultiTask::Handle::Wait() {
		_Wait();
	}

	void MultiTask::Handle::_Wait() {
		if (_handles != nullptr) {
			std::exception_ptr exception = nullptr;

			// Wait for all subtasks
			for (uint32_t i = 0u; i < _count; ++i) {
				try {
					_handles[i].Wait();
				} catch (...) {
					exception = std::current_exception();
				}
			}

			// Free memory used for subtasks
			for (uint32_t i = 0u; i < _count; ++i) {
				try {
					_subtasks[i].~SubTask();
				} catch (...) {
					exception = std::current_exception();
				}
				try {
					_handles[i].~UniqueTaskHandle();
				} catch (...) {
					exception = std::current_exception();
				}
			}
			operator delete(_subtasks);
			_subtasks = nullptr;
			_handles = nullptr;

			if (exception) std::rethrow_exception(exception);
		}
	}

}