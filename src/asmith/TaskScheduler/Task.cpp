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

#if ANVIL_DEBUG_TASKS
	#include <cctype>
	#include <chrono>
	#include <sstream>
#endif
#include <string>
#include <algorithm>
#include "asmith/TaskScheduler/Core.hpp"
#include "asmith/TaskScheduler/Scheduler.hpp"
#include "asmith/TaskScheduler/Task.hpp"

#if ANVIL_NO_EXECUTE_ON_WAIT
	#define ANVIL_USE_NEST_COUNTER 1
#else 
	#define ANVIL_USE_NEST_COUNTER 0
#endif

namespace anvil {

#if ANVIL_DEBUG_TASKS
	static std::ostream* g_debug_stream = &std::cout;

#if ANVIL_DEBUG_TASKS
	void Task::SetDebugStream(std::ostream& stream) {
		g_debug_stream = &stream;
	}
#endif

	static float GetDebugTime() {
		static const uint64_t g_reference_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		return static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - g_reference_time) / 1000000.f;
	}

	static std::string GetShortName(const Task* task) {
		return std::to_string(reinterpret_cast<uintptr_t>(task));
	}

	static std::string GetShortName(const Scheduler* scheduler) {
		return std::to_string(reinterpret_cast<uintptr_t>(scheduler));
	}

	static std::string FormatClassName(std::string name) {
		// Visual studio appends class to the start of the name, we dont need this
		auto i = name.find("class ");
		while (i != std::string::npos) {
			name.erase(i, 6);
			i = name.find("class ");
		}

		// Erase whitepace from name
		for (auto j = name.begin(); j != name.end(); ++j) {
			if (std::isspace(*j)) {
				name.erase(j);
				j = name.begin();
			}
		}

		return name;
	}

	static std::string GetLongName(const Task* task) {
		return FormatClassName(typeid(*task).name());
	}

	static std::string GetLongName(const Scheduler* scheduler) {
		return FormatClassName(typeid(*scheduler).name());
	}


	static void _PrintDebugMessage(std::string message) {
	}

	static void PrintDebugMessage(const Task* task, const Scheduler* scheduler, std::string message) {
		// Task name
		if (task) {
			// Replace task name
			auto i = message.find("%task%");
			if (i != std::string::npos) {
				std::string short_name = GetShortName(task);
				while (i != std::string::npos) {
					message.replace(i, 6, short_name);
					i = message.find("%task%");
				}
			}

			i = message.find("%task_class%");
			if (i != std::string::npos) {
				std::string long_name = GetLongName(task);
				while (i != std::string::npos) {
					message.replace(i, 12, long_name);
					i = message.find("%task_class%");
				}
			}
		}

		// Scheduler name
		if (scheduler) {

			// Replace task name
			auto i = message.find("%scheduler%");
			if (i != std::string::npos) {
				std::string short_name = GetShortName(scheduler);
				while (i != std::string::npos) {
					message.replace(i, 11, short_name);
					i = message.find("%scheduler%");
				}
			}

			i = message.find("%scheduler_class%");
			if (i != std::string::npos) {
				std::string long_name = GetLongName(scheduler);
				while (i != std::string::npos) {
					message.replace(i, 17, long_name);
					i = message.find("%scheduler_class%");
				}
			}
		}


		// Replace thread id
		auto i = message.find("%thread%");
		if (i != std::string::npos) {
			std::string thread = (std::ostringstream() << std::this_thread::get_id()).str();
			message.replace(i, 8, thread);
		}

		// Append line break
		if (message.back() != '\n') message += '\n';

		// Timestamp
		message = std::to_string(GetDebugTime()) + "ms : " + message;

		// Write the message
		static std::mutex g_lock;
		std::lock_guard<std::mutex> locl(g_lock);
		g_debug_stream->write(message.c_str(), message.size());
	}

	void Task::PrintDebugMessage(const char* message) const {
		anvil::PrintDebugMessage(this, nullptr, message);
	}

	void Scheduler::PrintDebugMessage(const char* message) const {
		anvil::PrintDebugMessage(nullptr, this, message);
	}

#else
	#define GetDebugTime() 0.f
#endif

#if ANVIL_USE_NEST_COUNTER
	thread_local int32_t g_tasks_nested_on_this_thread = 0; //!< Tracks if Task::Execute is being called on this thread (and how many tasks are nested)
#endif

#if ANVIL_TASK_GLOBAL_SCHEDULER_LIST
	static std::mutex g_scheduler_list_lock;
	enum { 
		INVALID_SCHEDULER = UINT8_MAX,
		MAX_SCHEDULERS = INVALID_SCHEDULER
	};
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
#else
#define INVALID_SCHEDULER nullptr
#endif

	// Task

	Task::Task() :
		_scheduler(INVALID_SCHEDULER),
		_priority(Priority::PRIORITY_MIDDLE),
		_state(STATE_INITIALISED)
	{
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Task %task% is created on thread %thread%");
#endif
	}

	Task::~Task() {
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Task %task% is destroyed on thread %thread%");
#endif
		//! \bug If the task is scheduled it must be removed from the scheduler
	}

	void Task::Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds) {
		if (_state != STATE_EXECUTING) throw std::runtime_error("Task cannot yeild unless it is in STATE_EXECUTING");

		if (condition()) return;

#if ANVIL_TASK_CALLBACKS
		OnBlock();
#endif

		const float debug_time = GetDebugTime();
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Task %task% paused on thread %thread% after executing for " + std::to_string(debug_time - _debug_timer) + " milliseconds");
#endif

		_state = STATE_BLOCKED;
		try {
			_GetScheduler()->Yield(condition, max_sleep_milliseconds);
		} catch (...) {
			_state = STATE_EXECUTING;
			std::rethrow_exception(std::current_exception());
		}
		_state = STATE_EXECUTING;

#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Task %task% resumed execution on thread %thread% after being paused for " + std::to_string(GetDebugTime() - debug_time) + " milliseconds");
#endif

#if ANVIL_TASK_CALLBACKS
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
			for(auto i = scheduler->_task_queue.begin(); i < scheduler->_task_queue.end(); ++i) {
				if (*i == this) {
					scheduler->_task_queue.erase(i);
					notify = true;
					break;
				}
			}
#if ANVIL_TASK_DELAY_SCHEDULING
			for (auto i = scheduler->_unready_task_queue.begin(); i < scheduler->_unready_task_queue.end(); ++i) {
				if (*i == this) {
					scheduler->_unready_task_queue.erase(i);
					notify = true;
					break;
				}
			}
#endif

#if ANVIL_DEBUG_TASKS
			anvil::PrintDebugMessage(this, nullptr, "Task %task% was canceled from thread %thread% after being scheduled for " + std::to_string(GetDebugTime() - _debug_timer) + " milliseconds");
#endif

#if ANVIL_TASK_CALLBACKS
			// Call the cancelation callback
			try {
				OnCancel();
			} catch (...) {
#if ANVIL_TASK_HAS_EXCEPTIONS
				_exception = std::current_exception();
#endif
			}
#endif
			// State change and cleanup
			_state = Task::STATE_CANCELED;
			_scheduler = INVALID_SCHEDULER;
		}

		// Notify anythign waiting for changes to the task queue
		if (notify) scheduler->TaskQueueNotify();

		// Canceled successfully
		return true;
	}

	void Task::Wait() {
		Scheduler* scheduler = _GetScheduler();
		if (scheduler == nullptr || _state == Task::STATE_COMPLETE || _state == Task::STATE_CANCELED) return;

		bool will_yield;
#if ANVIL_NO_EXECUTE_ON_WAIT
		// Only call yield if Wait is called from inside of a Task
		will_yield = g_tasks_nested_on_this_thread > 0;
#else
		will_yield = true;
#endif

		const float time = GetDebugTime();

		if (will_yield) {
#if ANVIL_DEBUG_TASKS
			anvil::PrintDebugMessage(this, nullptr, "Waiting on thread %thread% for Task %task% to complete execution");
#endif
			scheduler->Yield([this]()->bool {
				return _state == Task::STATE_COMPLETE || _state == Task::STATE_CANCELED;
			});
		} else {
#if ANVIL_DEBUG_TASKS
			anvil::PrintDebugMessage(this, nullptr, "Waiting on thread %thread% for Task %task% to complete execution without yielding");
#endif
			while (_state != Task::STATE_COMPLETE && _state != Task::STATE_CANCELED) {
				std::unique_lock<std::mutex> lock(scheduler->_mutex);
				scheduler->_task_queue_update.wait_for(lock, std::chrono::milliseconds(10));

			}
		}

#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Finished waiting on thread %thread% for Task %task% after " + std::to_string(GetDebugTime() - time) + " milliseconds");
#endif

#if ANVIL_TASK_HAS_EXCEPTIONS
		// Rethrow a caught exception
		if (_exception) {
			std::exception_ptr tmp = _exception;
			_exception = std::exception_ptr();
			std::rethrow_exception(tmp);
		}
#endif
	}


	void Task::SetPriority(const Priority priority) {
#if ANVIL_TASK_EXTENDED_PRIORITY
		ExtendedPriority new_priority;
		new_priority.base_priority = priority;
		new_priority.extended_priority = GetExtendedPriority();
#else
		const Priority new_priority = priority;
#endif

		std::exception_ptr exception = nullptr;
		Scheduler* scheduler = _GetScheduler();
		if (scheduler) {
			std::lock_guard<std::mutex> lock(scheduler->_mutex);
			if (_state == STATE_SCHEDULED) {
				_priority = new_priority;
				scheduler->SortTaskQueue();
			} else {
				exception = std::make_exception_ptr(std::runtime_error("Priority of a task cannot be changed when executing"));
				goto HANDLE_ERROR;
			}
		} else {
			_priority = new_priority;
		}


#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Priority of Task %task% was set to " + std::to_string(_priority) + " from thread %thread%");
#endif

		return;
HANDLE_ERROR:
		Cancel();
		std::rethrow_exception(exception);
	}

	Task::Priority Task::GetPriority() const throw() {
		return static_cast<Priority>(_priority);
	}

#if ANVIL_TASK_EXTENDED_PRIORITY
	uint32_t Task::GetExtendedPriority() const {
		return 0u;
	}
#endif 

	Scheduler* Task::_GetScheduler() const throw() {
#if ANVIL_TASK_GLOBAL_SCHEDULER_LIST
		if (_scheduler == INVALID_SCHEDULER) return nullptr;
		return g_scheduler_list[_scheduler];
#else
		return _scheduler;
#endif
	}

	void Task::Execute() throw() {
		const float time = GetDebugTime();
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Task %task% begins execution on thread %thread% after being scheduled for " + std::to_string(time - _debug_timer) + " milliseconds");
		_debug_timer = time;
#endif

#if ANVIL_USE_NEST_COUNTER
		++g_tasks_nested_on_this_thread; // Remember that Execute is being called
#endif

		// Remember the scheduler for later
		Scheduler* const scheduler = _GetScheduler();

		// Execute the task
		_state = Task::STATE_EXECUTING;
		try {
			OnExecution();
			_state = Task::STATE_COMPLETE;
		} catch (...) {
#if ANVIL_DEBUG_TASKS
			anvil::PrintDebugMessage(this, nullptr, "Task %task% threw exception on thread %thread% after executing for " + std::to_string(GetDebugTime() - _debug_timer) + " milliseconds");
#endif

#if ANVIL_TASK_HAS_EXCEPTIONS
			_exception = std::current_exception();
#endif
			_state = Task::STATE_CANCELED;
		}

		// Post-execution cleanup
		_scheduler = INVALID_SCHEDULER;

#if ANVIL_USE_NEST_COUNTER
		--g_tasks_nested_on_this_thread; // Execute is no longer being called
#endif

#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Task %task% finishes execution on thread %thread% after " + std::to_string(GetDebugTime() - time) + " milliseconds");
#endif

		// Wake waiting threads
		if (scheduler) scheduler->TaskQueueNotify();
	}

	// Scheduler

	Scheduler::Scheduler() {
#if ANVIL_TASK_GLOBAL_SCHEDULER_LIST
		AddScheduler(*this);
#endif
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(nullptr, this, "Scheduler %scheduler% has been created on thread %thread%");
#endif
	}

	Scheduler::~Scheduler() {
		//! \bug Scheduled tasks are left in an undefined state
#if ANVIL_TASK_GLOBAL_SCHEDULER_LIST
		RemoveScheduler(*this);
#endif
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(nullptr, this, "Scheduler %scheduler% has been destroyed on thread %thread%");
#endif
	}

#if ANVIL_TASK_DELAY_SCHEDULING
	void Scheduler::CheckUnreadyTasks() {
		// _mutex must be locked before calling this function

		size_t count = 0u;
		for (auto i = _unready_task_queue.begin(); i != _unready_task_queue.end(); ++i) {
			Task& t = **i;

			// If the task is now ready then add it to the ready queue
			if (t.IsReadyToExecute()) {
				_task_queue.push_back(&t);
				++count;
				_unready_task_queue.erase(i);
				i = _unready_task_queue.begin();
#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(&t, this, "Task %task% has become able to execute again after being scheduled for " + std::to_string(GetDebugTime() - t._debug_timer) + " milliseconds");
#endif
			}
		}

		// If the ready queue has been changed then make sure the tasks are in the correct order
		if (count > 0u) SortTaskQueue();
	}
#endif


	void Scheduler::TaskQueueNotify() {
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(nullptr, this, "Task queue on Scheduler %scheduler% has been updated, waking all threads");
#endif

#if ANVIL_TASK_DELAY_SCHEDULING
		// If the status of the task queue has changed then tasks may now be able to execute that couldn't before
		if(! _unready_task_queue.empty()) {
			std::lock_guard<std::mutex> lock(_mutex);
			CheckUnreadyTasks();
		}
#endif

		_task_queue_update.notify_all();
	}

	Task* Scheduler::RemoveNextTaskFromQueue() throw() {

		// Check if there are tasks before locking the queue
		// Avoids overhead of locking during periods of low activity
#if ANVIL_TASK_DELAY_SCHEDULING
		if (_task_queue.empty()) {
			// If there are no active tasks, check if an innactive one has now become ready
			if (_unready_task_queue.empty()) return false;

			std::lock_guard<std::mutex> lock(_mutex);
			CheckUnreadyTasks();

			if (_task_queue.empty()) return false;
		}
#else
		if (_task_queue.empty()) return false;
#endif

		Task* task = nullptr;
		bool notify = false;
		{
			// Lock the task queue so that other threads cannot access it
			std::lock_guard<std::mutex> lock(_mutex);

			while (task == nullptr) {
				// Check again that another thread hasn't emptied the queue while locking
				if (_task_queue.empty()) return false;

				// Remove the task at the back of the queue
				task = _task_queue.back();
				_task_queue.pop_back();

#if ANVIL_TASK_DELAY_SCHEDULING
				if (!task->IsReadyToExecute()) {
					// Add the task to the unready list
					_unready_task_queue.push_back(task);
					task = nullptr;
					notify = true;
#if ANVIL_DEBUG_TASKS
					anvil::PrintDebugMessage(&t, this, "Task %task% has become unable to execute after being scheduled for " + std::to_string(GetDebugTime() - t._debug_timer) + " milliseconds");
#endif
					continue;
				}
#endif
			}
		}

		// If something has happened to the task queue then notify yielding tasks
		if (notify) TaskQueueNotify();

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
				// If no task was able to be executed then block until there is a queue update
#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(nullptr, this, "Thread %thread% put to sleep because there was no work on Scheduler %scheduler%");
#endif
				std::unique_lock<std::mutex> lock(_mutex);
				if (max_sleep_milliseconds == UINT32_MAX) { // Special behaviour, only wake when task updates happen (useful for implementing a thread pool)
					_task_queue_update.wait(lock);
				} else {
					_task_queue_update.wait_for(lock, std::chrono::milliseconds(max_sleep_milliseconds));
				}
#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(nullptr, this, "Thread %thread% woke up");
#endif
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
			// Change state
			Task& t = *tasks[i];
			t._state = Task::STATE_SCHEDULED;

			// Initialise scheduling data
#if ANVIL_TASK_GLOBAL_SCHEDULER_LIST
			t._scheduler = GetSchedulerIndex(*this);
#else
			t._scheduler = this;
#endif

#if ANVIL_TASK_HAS_EXCEPTIONS
			t._exception = std::exception_ptr();
#endif

#if ANVIL_TASK_CALLBACKS
			// Task callback
			try {
				t.OnScheduled();
			} catch (...) {
#if ANVIL_TASK_HAS_EXCEPTIONS
				t._exception = std::current_exception();
#endif
				t.Cancel();
			}
#endif
		}

		// Add to task queue
#if ANVIL_TASK_DELAY_SCHEDULING
		size_t ready_count = 0u;
#else
		const uint32_t& ready_count = count;
#endif

		{
			std::lock_guard<std::mutex> lock(_mutex);
			for (uint32_t i = 0u; i < count; ++i) {
				Task& t = *tasks[i];

#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(&t, nullptr, "Type of Task %task% is %task_class%");
				anvil::PrintDebugMessage(&t, nullptr, "Task %task% is scheduled with Scheduler %scheduler% from thread %thread%");
				t._debug_timer = GetDebugTime();
#endif
#if ANVIL_TASK_DELAY_SCHEDULING
				// If the task isn't ready to execute yet push it to the innactive queue
				if (!t.IsReadyToExecute()) {
#if ANVIL_DEBUG_TASKS
					anvil::PrintDebugMessage(&t, this, "Task %task% is not ready to execute yet");
#endif
					_unready_task_queue.push_back(&t);
					continue;
				}
#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(&t, this, "Task %task% is ready to execute");
#endif
	
				// Add to the active queue
				++ready_count;
#endif
				_task_queue.push_back(&t);
			}

			// Sort task list by priority
			if (ready_count > 0u) SortTaskQueue();
		}

		// Notify waiting threads
		if (ready_count > 0u) TaskQueueNotify();
	}
	
	void Scheduler::Schedule(Task& task, Priority priority) {
		task.SetPriority(priority);
		Schedule(task);
	}
}