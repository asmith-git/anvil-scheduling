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

#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
	static std::mutex g_scheduler_list_lock;
	enum { MAX_SCHEDULERS = INT8_MAX };
	static Scheduler* g_scheduler_list[MAX_SCHEDULERS];

	namespace detail {
		// I don't think this is required on most compilers, but ensure memory is zeroed on program start

		static uint32_t InitialiseSchedulerList() throw() {
			memset(g_scheduler_list, 0, sizeof(Scheduler*) * MAX_SCHEDULERS);
			return 0;
		}

		static const uint32_t g_ensure_scheduler_list = InitialiseSchedulerList();
	}

	static uint32_t GetSchedulerIndex(const Scheduler& scheduler) {
		//! \bug Undefined behaviour if a scheduler is deleted while this function is running

		for (uint32_t i = 0u; i < MAX_SCHEDULERS; ++i) {
			if (g_scheduler_list[i] == &scheduler) return i;
		}

		throw std::runtime_error("Could not find scheduler in global list");
	}

	static uint32_t AddScheduler(Scheduler& scheduler) {
		std::lock_guard<std::mutex> lock(g_scheduler_list_lock);
		for (uint32_t i = 0u; i < MAX_SCHEDULERS; ++i) {
			if (g_scheduler_list[i] == nullptr) {
				g_scheduler_list[i] = &scheduler;
				return i;
			}
		}
		throw std::runtime_error("Too many schedulers in global list");
	}

	static void RemoveScheduler(const Scheduler& scheduler) {
		std::lock_guard<std::mutex> lock(g_scheduler_list_lock);
		for (uint32_t i = 0u; i < MAX_SCHEDULERS; ++i) {
			if (g_scheduler_list[i] == &scheduler) {
				g_scheduler_list[i] = nullptr;
				return;
			}
		}
		throw std::runtime_error("Could not find scheduler in global list");
	}
#endif

	// Task

	Task::Task() :
#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
		_scheduler_index(-1),
#else
		_scheduler(nullptr),
#endif
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
		OnBlock();
#endif

		_state = STATE_BLOCKED;
		try {
			_GetScheduler()->Yield(condition, max_sleep_milliseconds);
		} catch (...) {
			_state = STATE_EXECUTING;
			std::rethrow_exception(std::current_exception());
		}
		_state = STATE_EXECUTING;

#if ASMITH_TASK_CALLBACKS
		OnResume();
#endif
	}

	bool Task::Cancel() throw() {
		// If no scheduler is attached to this task then it cannot be canceled
		Scheduler* scheduler = _GetScheduler();
		if (scheduler == nullptr) return false;

		// Lock the scheduler's task queue
		bool notify = false;
		{
			std::lock_guard<std::mutex> lock(scheduler->_mutex);

			// If the state is not scheduled then it cannot be canceled
			if (_state != STATE_SCHEDULED) return false;

			// Remove the task from the queue
			//! \bug If the task cannot be found then something has gone wrong, but the code doesnt check for this
			{
				auto i = scheduler->_task_queue.begin();
				auto end = scheduler->_task_queue.end();
				while (i != end) {
					if (*i == this) {
						scheduler->_task_queue.erase(i);
						notify = true;
						break;
					}
					++i;
				}
			}

#if ASMITH_TASK_CALLBACKS
			// Call the cancelation callback
			try {
				OnCancel();
			} catch (...) {
#if ASMITH_TASK_HAS_EXCEPTIONS
				_exception = std::current_exception();
#endif
			}
#endif
			// State change and cleanup
			_state = Task::STATE_CANCELED;
#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
			_scheduler_index = -1;
#else
			_scheduler = nullptr;
#endif
		}

		// Notify anythign waiting for changes to the task queue
		if (notify) scheduler->_task_queue_update.notify_all();

		// Canceled successfully
		return true;
	}

	void Task::Wait() {
		Scheduler* scheduler = _GetScheduler();
		if (scheduler == nullptr || _state == Task::STATE_COMPLETE || _state == Task::STATE_CANCELED) return;

		scheduler->Yield([this]()->bool {
			return _state == Task::STATE_COMPLETE || _state == Task::STATE_CANCELED;
		});

#if ASMITH_TASK_HAS_EXCEPTIONS
		// Rethrow a caught exception
		if (_exception) {
			std::exception_ptr tmp = _exception;
			_exception = std::exception_ptr();
			std::rethrow_exception(tmp);
		}
#endif
	}


	void Task::SetPriority(const Priority priority) {
		std::exception_ptr exception = nullptr;
		Scheduler* scheduler = _GetScheduler();
		if (scheduler) {
			std::lock_guard<std::mutex> lock(scheduler->_mutex);
			if (_state == STATE_SCHEDULED) {
#if ASMITH_TASK_EXTENDED_PRIORITY
				try {
					_extended_priority = GetExtendedPriority();
				} catch (...) {
					exception = std::current_exception();
					goto HANDLE_ERROR;
				}
#endif
				_priority = priority;
				scheduler->SortTaskQueue();
			} else {
				exception = std::make_exception_ptr(std::runtime_error("Priority of a task cannot be changed when executing"));
				goto HANDLE_ERROR;
			}
		} else {
			_priority = priority;
		}

		return;
HANDLE_ERROR:
		Cancel();
		std::rethrow_exception(exception);
	}

#if ASMITH_TASK_EXTENDED_PRIORITY
	float Task::GetExtendedPriority() const {
		return 0.f;
	}
#endif 

	Scheduler* Task::_GetScheduler() const throw() {
#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
		if (_scheduler_index < 0) return nullptr;
		return g_scheduler_list[_scheduler_index];
#else
		return _scheduler;
#endif
	}

	void Task::Execute() throw() {
		// Remember the scheduler for later
		Scheduler* const scheduler = _GetScheduler();

		// Execute the task
		_state = Task::STATE_EXECUTING;
		try {
			OnExecution();
			_state = Task::STATE_COMPLETE;
		} catch (...) {
#if ASMITH_TASK_HAS_EXCEPTIONS
			_exception = std::current_exception();
#endif
			_state = Task::STATE_CANCELED;
		}

		// Post-execution cleanup
#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
		_scheduler_index = -1;
#else
		_scheduler = nullptr;
#endif

		// Wake waiting threads
		if(scheduler) scheduler->_task_queue_update.notify_all();
	}

	// Scheduler

	Scheduler::Scheduler() {
#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
		AddScheduler(*this);
#endif
	}

	Scheduler::~Scheduler() {
		//! \bug Scheduled tasks are left in an undefined state
#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
		RemoveScheduler(*this);
#endif
	}

	Task* Scheduler::RemoveNextTaskFromQueue() throw() {
		// Check if there are tasks before locking the queue
		// Avoids overhead of locking during periods of low activity
		if (_task_queue.empty()) return false;

		Task* task = nullptr;
		bool notify = false;
		{
			// Lock the task queue so that other threads cannot access it
			std::lock_guard<std::mutex> lock(_mutex);

			// Check again that another thread hasn't emptied the queue while locking
			if (_task_queue.empty()) return false;

			{
				// Remove tasks that have been canceled
				auto i = _task_queue.begin();
				auto end = _task_queue.end();
				while (i != end) {
					if ((**i)._state != Task::STATE_SCHEDULED) {
						_task_queue.erase(i);
						i = _task_queue.begin();
						end = _task_queue.end();
						notify = true;
					}
					++i;
				}
			}

#if ASMITH_TASK_DELAY_SCHEDULING
			// Scan the queue backwards for a task that is ready to execute
			auto i = _task_queue.rbegin();
			auto end = _task_queue.rend();
			while (i != end) {
				if ((**i).IsReadyToExecute()) {
					task = *i;
					_task_queue.erase(std::next(i).base());
					break;
				}
				++i;
			}
#else
			// Remove the task at the back of the queue
			task = _task_queue.back();
			_task_queue.pop_back();
#endif
		}

		// If something has happened to the task queue then notify yielding tasks
		if (notify) _task_queue_update.notify_all();

		// Return the task if one was found
		return task;
	}

	bool Scheduler::TryToExecuteTask() throw() {
		// Get the next task
		Task* task = RemoveNextTaskFromQueue();

		// If there isn't a task available then return
		if (task == nullptr) return false;

		// Execute the task
		task->Execute();
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
#if ASMITH_TASK_EXTENDED_PRIORITY
			return lhs->_priority < rhs->_priority ? true : lhs->_extended_priority < rhs->_extended_priority;

#else
			return lhs->_priority < rhs->_priority;
#endif
		});
	}

	void Scheduler::Schedule(Task** tasks, const uint32_t count) {
		// Initial error checking
		for (uint32_t i = 0u; i < count; ++i) {
			if (tasks[i]->_state != Task::STATE_INITIALISED) throw std::runtime_error("Task cannot be scheduled unless it is in STATE_INITIALISED");
		}

		for (uint32_t i = 0u; i < count; ++i) {
			// Change state
			Task& t = *tasks[i];
			t._state = Task::STATE_SCHEDULED;

			// Initialise scheduling data
#if ASMITH_TASK_GLOBAL_SCHEDULER_LIST
			t._scheduler_index = GetSchedulerIndex(*this);
#else
			t._scheduler = this;
#endif
#if ASMITH_TASK_HAS_EXCEPTIONS
			t._exception = std::exception_ptr();
#endif

#if ASMITH_TASK_CALLBACKS
			// Task callback
			try {
				t.OnScheduled();
			} catch (...) {
#if ASMITH_TASK_HAS_EXCEPTIONS
				t._exception = std::current_exception();
#endif
				t.Cancel();
			}
#endif
		}

		// Add to task queue
		{
			std::lock_guard<std::mutex> lock(_mutex);
			for (uint32_t i = 0u; i < count; ++i) {
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