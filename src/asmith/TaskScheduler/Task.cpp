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

// For the latest version, please visit https://github.com/asmith-git/anvil-scheduling

#include "asmith/TaskScheduler/Core.hpp"
#if ANVIL_DEBUG_TASKS
	#include <cctype>
	#include <chrono>
	#include <sstream>
	#include <iostream>
	#include <map>
#endif
#include <string>
#include <atomic>
#include <algorithm>
#include <deque>
#include <list>
#include "asmith/TaskScheduler/Scheduler.hpp"
#include "asmith/TaskScheduler/Task.hpp"
#include <emmintrin.h>

namespace anvil {


#if ANVIL_DEBUG_TASKS
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

	static void DefaultEventHandler(SchedulerDebugEvent* scheduler_event, TaskDebugEvent* task_event) {
		struct TaskDebugData {
			Task* task;
			float schedule_time;
			float execution_start_time;
			float execution_end_time;
			float pause_time;

			TaskDebugData() :
				task(nullptr),
				schedule_time(0.f),
				execution_start_time(0.f),
				execution_end_time(0.f),
				pause_time(0.f)
			{}
		};
		struct SchedulerDebugData {
			float pause_time;
		};
		static std::mutex g_debug_lock;
		static std::map<uint32_t, TaskDebugData> g_task_debug_data;
		static std::map<uint32_t, SchedulerDebugData> g_scheduler_debug_data;

		std::stringstream ss;

		if (scheduler_event) {
			std::lock_guard<std::mutex> lock(g_debug_lock);
			//ss << scheduler_event->time << " ms : ";

			switch (scheduler_event->type) {
			case SchedulerDebugEvent::EVENT_CREATE:
				g_scheduler_debug_data.emplace(scheduler_event->scheduler_id, SchedulerDebugData{ 0.f });
				ss << "Scheduler " << scheduler_event->scheduler_id << " was created";
				break;

			case SchedulerDebugEvent::EVENT_PAUSE:
				{
					SchedulerDebugData& debug = g_scheduler_debug_data[scheduler_event->scheduler_id];
					debug.pause_time = scheduler_event->time;
					ss << "Scheduler " << scheduler_event->scheduler_id << " pauses execution";
				}
				break;

			case SchedulerDebugEvent::EVENT_RESUME:
				{
					SchedulerDebugData& debug = g_scheduler_debug_data[scheduler_event->scheduler_id];
					ss << "Scheduler " << scheduler_event->scheduler_id << " resumes execution after sleeping for " << (scheduler_event->time - debug.pause_time) << " ms";
				}
				break;

			case SchedulerDebugEvent::EVENT_DESTROY:
				g_scheduler_debug_data.erase(g_scheduler_debug_data.find(scheduler_event->scheduler_id));
				ss << "Scheduler " << scheduler_event->scheduler_id << " is destroyed";
				break;
			};

			ss << " on thread " << scheduler_event->thread_id;
		}

		if(task_event) {
			std::lock_guard<std::mutex> lock(g_debug_lock);
			ss << task_event->time << " ms : ";

			switch(task_event->type) {
			case TaskDebugEvent::EVENT_CREATE:
				{
					TaskDebugData debug;
					debug.task = task_event->task;
					g_task_debug_data.emplace(task_event->task_id, debug);
				}
				ss << "Task " << task_event->task_id << " was created, type is " << GetLongName(task_event->task);
				break;

			case TaskDebugEvent::EVENT_SCHEDULE:
				{
					TaskDebugData& debug = g_task_debug_data[task_event->task_id];
					debug.schedule_time = task_event->time;
				}
				ss << "Task " << task_event->task_id;
				if (task_event->parent_id != 0u) ss << " (is a child of task " << task_event->parent_id << ")";
				ss << " was scheduled on scheduler " << task_event->scheduler_id;
				break;

			case TaskDebugEvent::EVENT_CANCEL:
				g_task_debug_data.erase(g_task_debug_data.find(task_event->task_id));
				ss << "Task " << task_event->task_id << " was canceled";
				break;

			case TaskDebugEvent::EVENT_EXECUTE_BEGIN:
				{
					TaskDebugData& debug = g_task_debug_data[task_event->task_id];
					debug.execution_start_time = task_event->time;

					ss << "Task " << task_event->task_id << " begins execution after being scheduled for " << (task_event->time - debug.schedule_time) << " ms";
				}
				break;

			case TaskDebugEvent::EVENT_PAUSE:
				{
					TaskDebugData& debug = g_task_debug_data[task_event->task_id];
					debug.pause_time = task_event->time;

					ss << "Task " << task_event->task_id << " pauses after executing for " << (task_event->time - debug.execution_start_time) << " ms";
					if (task_event->will_yield) ss << " and will yield";
					else ss << " without yielding";
				}
				break;

			case TaskDebugEvent::EVENT_RESUME:
				{
					TaskDebugData& debug = g_task_debug_data[task_event->task_id];
					debug.pause_time = task_event->time;

					ss << "Task " << task_event->task_id << " resumes execution after being paused for " << (task_event->time - debug.pause_time) << " ms";
				}
				break;

			case TaskDebugEvent::EVENT_EXECUTE_END:
				{
					TaskDebugData& debug = g_task_debug_data[task_event->task_id];
					debug.execution_end_time = task_event->time;

					ss << "Task " << task_event->task_id << " completes execution after " << (task_event->time - debug.execution_start_time) << " ms";
				}
				break;

			case TaskDebugEvent::EVENT_DESTROY:
				{
					TaskDebugData& debug = g_task_debug_data[task_event->task_id];
				}
				g_task_debug_data.erase(g_task_debug_data.find(task_event->task_id));
				ss << "Task " << task_event->task_id << " is destroyed";
				break;
			};

			ss << " on thread " << task_event->thread_id;
		}

		ss << "\n";
		std::cerr << ss.str();
	}

	static TaskDebugEvent::DebugEventHandler g_debug_event_handler = DefaultEventHandler;
#endif

#if ANVIL_TASK_FIBERS
	struct FiberData {
		LPVOID fiber;
		Task* task;
		const std::function<bool(void)>* yield_condition;
		
		FiberData() :
			fiber(nullptr),
			task(nullptr),
			yield_condition(nullptr)
		{}

		FiberData(Task* task) :
			fiber(nullptr),
			task(task),
			yield_condition(nullptr)
		{}
	};
#endif

	struct TaskThreadLocalData {
#if ANVIL_TASK_FIBERS
		std::deque<FiberData*> fiber_list;
		FiberData* current_fiber;
		LPVOID main_fiber;
#else
		std::vector<Task*> task_stack;
#endif
		uint32_t scheduler_index;
		bool is_worker_thread;

		TaskThreadLocalData() :
#if ANVIL_TASK_FIBERS
			current_fiber(nullptr),
			main_fiber(nullptr),
#endif
			scheduler_index(0u),
			is_worker_thread(false)
		{
#if ANVIL_TASK_FIBERS
			main_fiber = ConvertThreadToFiber(nullptr);
#endif
		}
		
		~TaskThreadLocalData() {
#if ANVIL_TASK_FIBERS
			// Delete old fibers
			for (FiberData* fiber : fiber_list) {
				DeleteFiber(fiber->fiber);
				delete fiber;
			}
			fiber_list.clear();
#endif
		}

#if ANVIL_TASK_FIBERS

	bool AreAnyFibersReady() const {
		if (!fiber_list.empty()) {
			auto end = fiber_list.end();
			auto i = std::find_if(fiber_list.begin(), end, [this](FiberData* fiber)->bool {
				return fiber != current_fiber && fiber->task != nullptr && (fiber->yield_condition == nullptr || (*fiber->yield_condition)());
			});
			return i != end;
		}

		return false;
	}

	bool SwitchToTask(FiberData& fiber) {
		if (&fiber == current_fiber) return false;
	
		// If the task is able to execute
		if (fiber.task != nullptr) {
			if (fiber.yield_condition == nullptr || (*fiber.yield_condition)()) {
				current_fiber = &fiber;
				SwitchToFiber(fiber.fiber);
				return true;
			}
		}
	
		return false;
	}
	
	bool SwitchToAnyTask() {
		// Try to execute a task that is ready to resume
		if (! fiber_list.empty()) {
			auto end = fiber_list.end();
			auto i = std::find_if(fiber_list.begin(), end, [this](FiberData* fiber)->bool {
				return fiber != current_fiber && fiber->task != nullptr && (fiber->yield_condition == nullptr || (*fiber->yield_condition)());
			});
			if (i != end) {
				// Move fiber to the back of the list
				FiberData* fiber = *i;
				fiber_list.erase(i);
				fiber_list.push_back(fiber);

				// Switch to the fiber
				current_fiber = fiber;
				SwitchToFiber(fiber->fiber);
				return true;
			}
		}
	
		return false;
	}
		
	void SwitchToMainFiber() {
		// Are we currently executing the main fiber?
		if (current_fiber == nullptr) return;
		
		// Switch to it
		current_fiber = nullptr;
		SwitchToFiber(main_fiber);
	}
#endif
	};
	
	thread_local TaskThreadLocalData g_thread_additional_data;

#if ANVIL_USE_NEST_COUNTER
	thread_local int32_t g_tasks_nested_on_this_thread = 0; //!< Tracks if Task::Execute is being called on this thread (and how many tasks are nested)
#endif

#if ANVIL_DEBUG_TASKS

	static float GetDebugTime() {
		static const uint64_t g_reference_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		return static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - g_reference_time) / 1000000.f;
	}

	static std::atomic_uint32_t g_task_debug_id = 0u;
	static std::atomic_uint32_t g_scheduler_debug_id = 0u;
#endif

#define INVALID_SCHEDULER nullptr

	// Task

	Task::Task() :
		_scheduler(INVALID_SCHEDULER),
#if ANVIL_TASK_FAST_CHILD_COUNT || ANVIL_TASK_PARENT
		_fast_child_count(0u),
		_fast_recursive_child_count(0u),
		_nesting_depth(0u),
#endif
		_priority(Priority::PRIORITY_MIDDLE),
		_state(STATE_INITIALISED),
		_scheduled_flag(0u),
		_execute_begin_flag(0u),
		_execute_end_flag(0u),
		_wait_flag(0u)
	{
#if ANVIL_DEBUG_TASKS
		_debug_id = g_task_debug_id++;
		{
			TaskDebugEvent e = TaskDebugEvent::CreateEvent(_debug_id, this);
			g_debug_event_handler(nullptr, &e);
		}
#endif
	}

	Task::~Task() {
		{
			if (_state == STATE_SCHEDULED || _state == STATE_EXECUTING || _state == STATE_BLOCKED) {
				bool not_finished = true;
				while (not_finished) {
					// Wait for task to complete
					std::shared_lock<std::shared_mutex> lock(_lock);
					not_finished = _wait_flag == 0u;
				}
			}
		}

#if ANVIL_TASK_FAST_CHILD_COUNT || ANVIL_TASK_PARENT
		// Make sure children are destroyed first
		while (_fast_child_count + _fast_recursive_child_count > 0u);

		// Remove task from list of children
		Task* parent = _parent;
		if (parent) {
			{
				std::lock_guard<std::shared_mutex> lock(parent->_lock);
#if ANVIL_TASK_PARENT
				auto end = parent->_children.end();
				auto i = std::find(parent->_children.begin(), end, this);
				_parent->_children.erase(i);
#endif

				--_parent->_fast_child_count;
				--parent->_fast_recursive_child_count;
				parent = parent->_parent;
			}

			while (parent) {
				std::lock_guard<std::shared_mutex> lock(parent->_lock);
				--parent->_fast_recursive_child_count;
				parent = parent->_parent;
			}
		}
#endif
#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::DestroyEvent(_debug_id);
			g_debug_event_handler(nullptr, &e);
		}
#endif
		//! \bug If the task is scheduled it must be removed from the scheduler
	}

	Task* Task::GetCurrentlyExecutingTask() {
#if ANVIL_TASK_FIBERS
		if (g_thread_additional_data.current_fiber == nullptr) {
			return nullptr;
		} else {
			return g_thread_additional_data.current_fiber->task;
		}
#else
		if (g_thread_additional_data.task_stack.empty()) {
			return nullptr;
		} else {
			return g_thread_additional_data.task_stack.back();
		}
#endif
	}

	size_t Task::GetNumberOfTasksExecutingOnThisThread() {
#if ANVIL_TASK_FIBERS
		size_t count = 0u;
		for (const FiberData* fiber : g_thread_additional_data.fiber_list) {
			if (fiber->task != nullptr) ++count;
		}
		return count;
#else
		return g_thread_additional_data.task_stack.size();
#endif
	}

	Task* Task::GetCurrentlyExecutingTask(size_t index) {
#if ANVIL_TASK_FIBERS
		size_t count = 0u;
		for (const FiberData* fiber : g_thread_additional_data.fiber_list) {
			if (fiber->task != nullptr) {
				if (count == index) return fiber->task;
				++count;
			}
		}
		return nullptr;
#else
		if (index >= g_thread_additional_data.task_stack.size()) return nullptr;
		return g_thread_additional_data.task_stack[index];
#endif
	}

	void Task::SetException(std::exception_ptr exception) {
#if ANVIL_TASK_HAS_EXCEPTIONS
		_exception = exception;
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
			for (auto i = scheduler->_task_queue.begin(); i < scheduler->_task_queue.end(); ++i) {
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
		}
		{
			std::lock_guard<std::shared_mutex> task_lock(_lock);
#if ANVIL_DEBUG_TASKS
			{
				TaskDebugEvent e = TaskDebugEvent::CancelEvent(_debug_id);
				g_debug_event_handler(nullptr, &e);
			}
#endif

#if ANVIL_TASK_CALLBACKS
			// Call the cancelation callback
			try {
				OnCancel();
			} catch (...) {
				SetException(std::current_exception());
			}
			
#endif
			// State change and cleanup
			_state = Task::STATE_CANCELED;
			_scheduler = INVALID_SCHEDULER;

			_wait_flag = 1u;
		}

		// Notify anythign waiting for changes to the task queue
		if (notify) scheduler->TaskQueueNotify();
		return true;
	}

	void Task::Wait() {
		Scheduler* scheduler = _GetScheduler();
		if (scheduler == nullptr || _state == Task::STATE_COMPLETE || _state == Task::STATE_CANCELED) return;

		const bool will_yield = scheduler->_no_execution_on_wait ?
			GetParent() != nullptr || GetNumberOfTasksExecutingOnThisThread() > 0 :	// Only call yield if Wait is called from inside of a Task
			true;																	// Always yield

#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::PauseEvent(_debug_id, will_yield);
			g_debug_event_handler(nullptr, &e);
		}
#endif

		const auto YieldCondition = [this]()->bool {
			std::shared_lock<std::shared_mutex> task_lock(_lock);
			if (_state != STATE_CANCELED && _state != STATE_COMPLETE) return false;
			return _wait_flag == 1u;
		};

		if (will_yield) {
			scheduler->Yield([this, YieldCondition]()->bool {
				return YieldCondition();
			});
		} else {
			while (! YieldCondition()) {
				// Wait for 1ms then check again
				std::unique_lock<std::mutex> lock(scheduler->_mutex);
				if (YieldCondition()) break; // If the condition was met while acquiring the lock
				scheduler->_task_queue_update.wait_for(lock, std::chrono::milliseconds(1));
			}
		}

#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::ResumeEvent(_debug_id);
			g_debug_event_handler(nullptr, &e);
		}
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


	void Task::SetPriority(Priority priority) {

		if constexpr(Priority::PRIORITY_HIGHEST > 255u) priority = static_cast<Priority>(priority +GetNestingDepth());

		std::exception_ptr exception = nullptr;
		Scheduler* scheduler = _GetScheduler();
		if (scheduler) {
			std::lock_guard<std::mutex> lock(scheduler->_mutex);
			if (_state == STATE_SCHEDULED) {
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

	void Task::Execute() throw() {
		// Remember the scheduler for later
		Scheduler* const scheduler = _GetScheduler();

		const auto CatchException = [this](std::exception_ptr&& exception, bool set_exception) {
			// Handle the exception
#if ANVIL_TASK_HAS_EXCEPTIONS
			if (set_exception) this->SetException(std::move(exception));
#endif
			// If the exception was caught after the task finished execution
			if (_state == STATE_COMPLETE || _state == STATE_CANCELED) {
				// Do nothing

			// If the exception was caught before or during execution
			} else {			
				// Cancel the Task
				_state = Task::STATE_CANCELED;
#if ANVIL_TASK_CALLBACKS
				// Call the cancelation callback
				try {
					OnCancel();
				} catch (std::exception& e) {
#if ANVIL_TASK_HAS_EXCEPTIONS
					// Task caught during execution takes priority as it probably has more useful debugging information
					if (!set_exception) this->SetException(std::current_exception());
#endif
				} catch (...) {
#if ANVIL_TASK_HAS_EXCEPTIONS
					// Task caught during execution takes priority as it probably has more useful debugging information
					if (!set_exception) this->SetException(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception")));
#endif
				}
#endif
			}
		};
#if ANVIL_TASK_FIBERS
		FiberData* fiber = nullptr;
		try {
			// Check if an existing fiber is unused
			for (FiberData* f : g_thread_additional_data.fiber_list) {
				if (f->task == nullptr) {
					fiber = f;
					break;
				}
			}

			if (fiber == nullptr) {
				// Allocate a new fiber
				g_thread_additional_data.fiber_list.push_back(new FiberData());
				fiber = g_thread_additional_data.fiber_list.back();
				fiber->fiber = CreateFiber(0u, Task::FiberFunction, fiber);
			}

			fiber->task = this;
			fiber->yield_condition = nullptr;
		}
		catch (std::exception& e) {
			CatchException(std::move(std::current_exception()), false);
		}

#else
		try {
			g_thread_additional_data.task_stack.push_back(this);
		} catch (std::exception& e) {
			CatchException(std::move(std::current_exception()), false);
		}
#endif
		Scheduler::ThreadDebugData* debug_data = _scheduler->GetDebugDataForThread(g_thread_additional_data.scheduler_index);
		if (debug_data) {
			++debug_data->tasks_executing;
			++_scheduler->_scheduler_debug.total_tasks_executing;
		}

		_execute_begin_flag = 1;
#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::ExecuteBeginEvent(_debug_id);
			g_debug_event_handler(nullptr, &e);
		}
#endif

		// Switch control to the task's fiber
#if ANVIL_TASK_FIBERS
		g_thread_additional_data.SwitchToTask(*fiber);
#else
		FiberFunction(*g_thread_additional_data.task_stack.back());
#endif
	}

#if ANVIL_TASK_FIBERS
	void WINAPI Task::FiberFunction(LPVOID param) {
		FiberData& fiber = *static_cast<FiberData*>(param);
		while (true) {
			Task& task = *fiber.task;
#else
	void Task::FiberFunction(Task& task) {
		if (true) {
#endif

			Scheduler& scheduler = task.GetScheduler();
			{
				const auto CatchException = [&task](std::exception_ptr&& exception, bool set_exception) {
					// Handle the exception
					if (set_exception) task.SetException(std::move(exception));
					// If the exception was caught after the task finished execution
					if (task._state == STATE_COMPLETE || task._state == STATE_CANCELED) {
						// Do nothing

					// If the exception was caught before or during execution
					} else {
						// Cancel the Task
						task._state = Task::STATE_CANCELED;
		#if ANVIL_TASK_CALLBACKS
						// Call the cancelation callback
						try {
							task.OnCancel();
						}
						catch (std::exception& e) {
							// Task caught during execution takes priority as it probably has more useful debugging information
							if (!set_exception) task.SetException(std::current_exception());
						}
						catch (...) {
							// Task caught during execution takes priority as it probably has more useful debugging information
							if (!set_exception) task.SetException(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception")));
						}
		#endif
					}
				};

				// If an error hasn't been detected yet
				if (task._state != Task::STATE_CANCELED) {

					// Execute the task
					{
						std::lock_guard<std::shared_mutex> task_lock(task._lock);
						task._state = Task::STATE_EXECUTING;
					}
					try {
						task.OnExecution();
					} catch (std::exception& e) {
						CatchException(std::move(std::current_exception()), true);
					} catch (...) {
						CatchException(std::exception_ptr(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception"))), true);
					}
				}

				Scheduler::ThreadDebugData* debug_data = task._scheduler->GetDebugDataForThread(g_thread_additional_data.scheduler_index);
				if (debug_data) {
					--debug_data->tasks_executing;
					--task._scheduler->_scheduler_debug.total_tasks_executing;
				}

				{
					// Post-execution cleanup
					std::lock_guard<std::shared_mutex> task_lock(task._lock);

					try {
#if ANVIL_TASK_FIBERS
						fiber.task = nullptr;
#else
						g_thread_additional_data.task_stack.pop_back();
#endif
					} catch (std::exception& e) {
						CatchException(std::move(std::current_exception()), false);
					}

					task._scheduler = INVALID_SCHEDULER;
					task._execute_end_flag = 1;
#if ANVIL_DEBUG_TASKS
					{
						TaskDebugEvent e = TaskDebugEvent::ExecuteEndEvent(task._debug_id);
						g_debug_event_handler(nullptr, &e);
					}
#endif
					task._state = Task::STATE_COMPLETE;

					task._wait_flag = 1u;
				}
			}

			scheduler.TaskQueueNotify();

			// Return control to the main thread
#if ANVIL_TASK_FIBERS
			g_thread_additional_data.SwitchToMainFiber();
#endif
		}
	}

	// Scheduler

	Scheduler::Scheduler(size_t thread_count) :
		_no_execution_on_wait(ANVIL_NO_EXECUTE_ON_WAIT ? true : false)

	{
		_scheduler_debug.thread_debug_data = thread_count == 0u ? nullptr : new ThreadDebugData[thread_count];
		_scheduler_debug.total_thread_count = 0u;
		_scheduler_debug.executing_thread_count = 0u;
		_scheduler_debug.sleeping_thread_count = 0u;
		_scheduler_debug.total_tasks_executing = 0u;
		_scheduler_debug.total_tasks_queued = 0u;

#if ANVIL_DEBUG_TASKS
		_debug_id = g_scheduler_debug_id++;
		{
			SchedulerDebugEvent e = SchedulerDebugEvent::CreateEvent(_debug_id);
			g_debug_event_handler(&e, nullptr);
		}
#endif
	}

	Scheduler::~Scheduler() {
		//! \bug Scheduled tasks are left in an undefined state
#if ANVIL_DEBUG_TASKS
		{
			SchedulerDebugEvent e = SchedulerDebugEvent::DestroyEvent(_debug_id);
			g_debug_event_handler(&e, nullptr);
		}
#endif
		if (_scheduler_debug.thread_debug_data) {
			delete[] _scheduler_debug.thread_debug_data;
			_scheduler_debug.thread_debug_data = nullptr;
		}
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
			}
		}

		// If the ready queue has been changed then make sure the tasks are in the correct order
		if (count > 0u) SortTaskQueue();
	}
#endif


	void Scheduler::TaskQueueNotify() {
#if ANVIL_TASK_DELAY_SCHEDULING
		// If the status of the task queue has changed then tasks may now be able to execute that couldn't before
		if(! _unready_task_queue.empty()) {
			std::lock_guard<std::mutex> lock(_mutex);
			CheckUnreadyTasks();
		}
#endif

		_task_queue_update.notify_all();
	}

	void Scheduler::RemoveNextTaskFromQueue(Task** tasks, uint32_t& count) throw() {
#if ANVIL_TASK_DELAY_SCHEDULING
		// Check if there are tasks before locking the queue
		// Avoids overhead of locking during periods of low activity
		if (_task_queue.empty()) {
			// If there are no active tasks, check if an innactive one has now become ready
			if (_unready_task_queue.empty()) {
				count = 0u;
				return;
			}

			std::lock_guard<std::mutex> lock(_mutex);
			CheckUnreadyTasks();

			if (_task_queue.empty()) {
				count = 0u;
				return;
			}
		}

		Task* task = nullptr;
		bool notify = false;
		{
			// Lock the task queue so that other threads cannot access it
			std::lock_guard<std::mutex> lock(_mutex);

			while (task == nullptr) {
				// Check again that another thread hasn't emptied the queue while locking
				if (_task_queue.empty()) {
					count = 0u;
					return;
				}

				// Remove the task at the back of the queue
				task = _task_queue.back();
				_task_queue.pop_back();

#if ANVIL_TASK_DELAY_SCHEDULING
				if (!task->IsReadyToExecute()) {
					// Add the task to the unready list
					_unready_task_queue.push_back(task);
					task = nullptr;
					notify = true;
					continue;
				}
#endif
			}
		}

		// If something has happened to the task queue then notify yielding tasks
		if (notify) TaskQueueNotify();

		// Return the task if one was found
		tasks[0u] = task;
		count = 1u;
#else
		// Check if there are tasks before locking the queue
		// Avoids overhead of locking during periods of low activity
		if (_task_queue.empty()) {
			count = 0u;
			return;
		}

		{
			// Acquire the queue lock
			std::lock_guard<std::mutex> lock(_mutex);

			// Remove the last task(s) in the queue
			uint32_t count2 = 0u;
			while (count2 < count && !_task_queue.empty()) {
				tasks[count2++] = _task_queue.back();
				_task_queue.pop_back();
			}

			count = count2;
		}
#endif
	}

	bool Scheduler::TryToExecuteTask() throw() {
#if ANVIL_TASK_FIBERS
		// Try to resume execution of an existing task
		if(g_thread_additional_data.SwitchToAnyTask()) return true;

		enum { MAX_TASKS = 1u };
#else
		enum { MAX_TASKS = 32u };
#endif
		// Try to start the execution of a new task
		Task* tasks[MAX_TASKS];
		uint32_t task_count = static_cast<uint32_t>(_task_queue.size()) / _scheduler_debug.total_thread_count;
		if (task_count < 1) task_count = 1u;
		if (task_count > MAX_TASKS) task_count = MAX_TASKS;
		RemoveNextTaskFromQueue(tasks, task_count);

		// If there is a task available then execute it
		if (task_count > 0) {

			for (uint32_t i = 0u; i < task_count; ++i) {
				tasks[i]->Execute();
			}

			return true;
		}

		return false;
	}


	void Scheduler::RegisterAsWorkerThread() {
		TaskThreadLocalData& local_data = g_thread_additional_data;
		local_data.is_worker_thread = true;

		{
			std::unique_lock<std::mutex> lock(_mutex);
			local_data.scheduler_index = _scheduler_debug.total_thread_count++;
			ThreadDebugData& debug_data = _scheduler_debug.thread_debug_data[local_data.scheduler_index];
			debug_data.tasks_executing = 0u;
			debug_data.sleeping = 0u;
			debug_data.enabled = 1u;
			++_scheduler_debug.executing_thread_count; // Default state is executing
		}
	}

	void Scheduler::Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds) {
		// If the condition is already met then avoid the overheads of suspending the thread / task
		if (condition()) return;

		max_sleep_milliseconds = std::max(1u, max_sleep_milliseconds);

		// If this function is being called by a task
#if ANVIL_TASK_FIBERS
		FiberData* fiber = g_thread_additional_data.current_fiber;
		Task* t = fiber == nullptr ? nullptr : fiber->task;
#else
		Task* t = g_thread_additional_data.task_stack.empty() ? nullptr : g_thread_additional_data.task_stack.back();
#endif
		Scheduler::ThreadDebugData* debug_data = GetDebugDataForThisThread();

#if ANVIL_DEBUG_TASKS
		const float debug_time = GetDebugTime();
#endif
		if (t) {
			std::lock_guard<std::shared_mutex> lock(t->_lock);

			// State change
			if (t->_state != Task::STATE_EXECUTING) throw std::runtime_error("anvil::Scheduler::Yield : Task cannot yield unless it is in STATE_EXECUTING");
			t->_state = Task::STATE_BLOCKED;
#if ANVIL_TASK_CALLBACKS
			t.OnBlock();
#endif


#if ANVIL_TASK_FIBERS
			// Remember how the task should be resumed
			fiber->yield_condition = &condition;
#endif
		}

		// While the condition is not met
		while (true) {
			// Check the yield condition has been met yet
			if (condition()) {
EXIT_CONDITION:
				break;
			}

			// If the thread is enabled
			if (!(debug_data && debug_data->enabled == 0u)) { 
				// Try to execute a task
				if (TryToExecuteTask()) {
					// Check the yield condition has been met yet
					if (condition()) goto EXIT_CONDITION;

				} else {
					// Block until there is a queue update
					std::unique_lock<std::mutex> lock(_mutex);

#if ANVIL_DEBUG_TASKS
					{
						SchedulerDebugEvent e = SchedulerDebugEvent::PauseEvent(_debug_id);
						g_debug_event_handler(&e, nullptr);
					}
#endif
					// Update that thread is sleeping
					if (debug_data) {
						debug_data->sleeping = 1u;
						--_scheduler_debug.executing_thread_count;
					}

					const auto predicate = [this, &condition]()->bool {
#if ANVIL_TASK_FIBERS
						if (g_thread_additional_data.AreAnyFibersReady()) return true;
#endif
						return condition() || !_task_queue.empty();
					};

					if (max_sleep_milliseconds == UINT32_MAX) { // Special behaviour, only wake when task updates happen (useful for implementing a thread pool)
						_task_queue_update.wait(lock, predicate);
					} else {
						_task_queue_update.wait_for(lock, std::chrono::milliseconds(max_sleep_milliseconds), predicate);
					}

					// Update that the thread is running
					if (debug_data) {
						debug_data->sleeping = 0u;
						++_scheduler_debug.executing_thread_count;
					}

#if ANVIL_DEBUG_TASKS
					{
						SchedulerDebugEvent e = SchedulerDebugEvent::ResumeEvent(_debug_id);
						g_debug_event_handler(&e, nullptr);
					}
#endif
				}

			}
		}

		// If this function is being called by a task
		if (t) {
			std::lock_guard<std::shared_mutex> lock(t->_lock);

#if ANVIL_TASK_FIBERS
			// The task can no longer be resumed
			fiber->yield_condition = nullptr;
#endif

			// State change
			t->_state = Task::STATE_EXECUTING;

#if ANVIL_TASK_CALLBACKS
			t->OnResume();
#endif
		}
	}

	void Scheduler::SortTaskQueue() throw() {
		std::sort(_task_queue.begin(), _task_queue.end(), [](const Task* lhs, const Task* rhs)->bool {
			return lhs->_priority < rhs->_priority;
		});
	}


	void Scheduler::Schedule(std::shared_ptr<Task>* tasks, uint32_t count) {
		enum { TASK_BLOCK = 1024 };
		Task* tasks2[TASK_BLOCK];
		while (count > 0) {
			const uint32_t tasks_to_add = count > TASK_BLOCK ? TASK_BLOCK : count;

			for (uint32_t i = 0u; i < tasks_to_add; ++i) tasks2[i] = tasks[i].get();
			Schedule(tasks2, tasks_to_add);

			tasks += tasks_to_add;
			count -= tasks_to_add;
		}
	}

	void Scheduler::Schedule(Task** tasks, const uint32_t count) {
		const auto this_scheduler = this;

#if ANVIL_TASK_PARENT || ANVIL_TASK_FAST_CHILD_COUNT
#if ANVIL_TASK_FIBERS
		Task* const parent = g_thread_additional_data.current_fiber == nullptr ? nullptr : g_thread_additional_data.current_fiber->task;
#else
		Task* const parent = g_thread_additional_data.task_stack.empty() ? nullptr : g_thread_additional_data.task_stack.back();
#endif
#endif

		// Initial error checking and initialisation
		for (uint32_t i = 0u; i < count; ++i) {
			Task& t = *tasks[i];
			std::lock_guard<std::shared_mutex> task_lock(t._lock);

			if (t._state != Task::STATE_INITIALISED) continue;

			// Change state
			t._scheduled_flag = 1u;
			t._state = Task::STATE_SCHEDULED;
			t._wait_flag = 0u;

			// Initialise scheduling data
			t._scheduler = this_scheduler;

#if ANVIL_TASK_HAS_EXCEPTIONS
			t._exception = std::exception_ptr();
#endif

#if ANVIL_TASK_PARENT || ANVIL_TASK_FAST_CHILD_COUNT
			t._nesting_depth = 0u;
			// Update the child / parent relationship between tasks
			t._parent = parent;
			if (parent) {
				anvil::Task* parent2;
				{
					std::lock_guard<std::shared_mutex> parent_lock(parent->_lock);
#if ANVIL_TASK_PARENT
					parent->_children.push_back(tasks[i]);
#endif
					++t._nesting_depth;
					++parent->_fast_child_count;
					++parent->_fast_recursive_child_count;
					parent2 = parent->_parent;
				}
				while (parent2) {
					std::lock_guard<std::shared_mutex> parent2_lock(parent2->_lock);
					++t._nesting_depth;
					++parent2->_fast_recursive_child_count;
					parent2 = parent2->_parent;
				}
			}
#endif

			// Calculate extended priority
#if ANVIL_TASK_EXTENDED_PRIORITY
			try {
				t._priority.extended = t.CalculateExtendedPriorty();
			} catch (std::exception& e) {
				t.SetException(std::current_exception());
				t.Cancel();
				continue;
			} catch (...) {
				t.SetException(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception")));
				t.Cancel();
				continue;
			}
#endif

#if ANVIL_TASK_CALLBACKS
			// Task callback
			try {
				t.OnScheduled();
			} catch (std::exception& e) {
				t.SetException(std::current_exception());
				t.Cancel();
				continue;
			} catch (...) {
				t.SetException(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception")));
				t.Cancel();
				continue;
			}
#endif
		}

		// Add to task queue
		size_t ready_count = 0u;

		{
			// Lock the task queue
			std::lock_guard<std::mutex> lock(_mutex);

			// For each task to be scheduled
			for (uint32_t i = 0u; i < count; ++i) {
				Task& t = *tasks[i];

				// Skip the task if initalisation failed
				if (t._scheduler != this_scheduler) continue;
#if ANVIL_TASK_DELAY_SCHEDULING
				// If the task isn't ready to execute yet push it to the innactive queue
				if (!t.IsReadyToExecute()) {
					_unready_task_queue.push_back(&t);
					continue;
				}
#endif
#if ANVIL_DEBUG_TASKS
				{
					uint32_t parent_id = 0u;
#if ANVIL_TASK_PARENT || ANVIL_TASK_FAST_CHILD_COUNT
					if (parent) parent_id = parent->_debug_id;
#endif
					TaskDebugEvent e = TaskDebugEvent::ScheduleEvent(t._debug_id, parent_id, _debug_id);
					g_debug_event_handler(nullptr, &e);
				}
#endif

				// Add to the active queue
				++ready_count;
				_task_queue.push_back(tasks[i]);
			}

			// Sort task list by priority
			if (ready_count > 0u) SortTaskQueue();
		}

		// Notify waiting threads
		if (ready_count > 0u) TaskQueueNotify();
	}
	
	void Scheduler::Schedule(Task* task, Priority priority) {
		task->SetPriority(priority);
		Schedule(&task, 1u);
	}

#if ANVIL_TASK_EXTENDED_PRIORITY
	void Scheduler::RecalculatedExtendedPriorities() {
		std::lock_guard<std::mutex> lock(_mutex);
		for (Task* t : _task_queue) {
			t->_priority.extended = t->CalculateExtendedPriorty();
		}
		SortTaskQueue();
	}
#endif

	uint32_t Scheduler::GetThisThreadIndex() const {
		return g_thread_additional_data.scheduler_index;
	}

#if ANVIL_DEBUG_TASKS
	// TaskDebugEvent

	static uint32_t GetDebugThreadID() {
		std::stringstream ss;
		ss << std::this_thread::get_id();
		uint32_t id;
		ss >> id;
		return id;
	}

	thread_local const uint32_t g_debug_thread_id = GetDebugThreadID();

	TaskDebugEvent TaskDebugEvent::CreateEvent(uint32_t task_id, Task* task) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.task = task;
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.type = EVENT_CREATE;
		return e;
	}

	TaskDebugEvent TaskDebugEvent::ScheduleEvent(uint32_t task_id, uint32_t parent_id, uint32_t scheduler_id) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.parent_id = parent_id;
		e.scheduler_id = scheduler_id;
		e.type = EVENT_SCHEDULE;
		return e;
	}

	TaskDebugEvent TaskDebugEvent::CancelEvent(uint32_t task_id) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.type = EVENT_CANCEL;
		return e;
	}

	TaskDebugEvent TaskDebugEvent::ExecuteBeginEvent(uint32_t task_id) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.type = EVENT_EXECUTE_BEGIN;
		return e;
	}

	TaskDebugEvent TaskDebugEvent::PauseEvent(uint32_t task_id, bool will_yield) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.will_yield = will_yield;
		e.type = EVENT_PAUSE;
		return e;
	}

	TaskDebugEvent TaskDebugEvent::ResumeEvent(uint32_t task_id) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.type = EVENT_RESUME;
		return e;
	}

	TaskDebugEvent TaskDebugEvent::ExecuteEndEvent(uint32_t task_id) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.type = EVENT_EXECUTE_END;
		return e;
	}

	TaskDebugEvent TaskDebugEvent::DestroyEvent(uint32_t task_id) {
		TaskDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.task_id = task_id;
		e.type = EVENT_DESTROY;
		return e;
	}

	void TaskDebugEvent::SetDebugEventHandler(DebugEventHandler handler) {
		if (handler == nullptr) handler = DefaultEventHandler;
		g_debug_event_handler = handler;
	}
	// SchedulerDebugEvent

	SchedulerDebugEvent SchedulerDebugEvent::CreateEvent(uint32_t scheduler_id) {
		SchedulerDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.scheduler_id = scheduler_id;
		e.type = EVENT_CREATE;
		return e;
	}

	SchedulerDebugEvent SchedulerDebugEvent::PauseEvent(uint32_t scheduler_id) {
		SchedulerDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.scheduler_id = scheduler_id;
		e.type = EVENT_PAUSE;
		return e;
	}

	SchedulerDebugEvent SchedulerDebugEvent::ResumeEvent(uint32_t scheduler_id) {
		SchedulerDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.scheduler_id = scheduler_id;
		e.type = EVENT_RESUME;
		return e;
	}

	SchedulerDebugEvent SchedulerDebugEvent::DestroyEvent(uint32_t scheduler_id) {
		SchedulerDebugEvent e;
		e.time = GetDebugTime();
		e.thread_id = g_debug_thread_id;
		e.scheduler_id = scheduler_id;
		e.type = EVENT_DESTROY;
		return e;
	}

#endif
}