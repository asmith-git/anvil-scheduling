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

#pragma optimize("", off)

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

	static float GetTimeMS() {
		static const uint64_t g_reference_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		return static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - g_reference_time) / 1000000.f;
	}


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

	// Task::TaskSchedulingData

	TaskSchedulingData::TaskSchedulingData() :
		task(nullptr),
		scheduler(nullptr),
		priority(Priority::PRIORITY_MIDDLE),
		state(Task::STATE_INITIALISED),
		scheduled_flag(0u),
		wait_flag(0u)
	{}

	// Task

	Task::Task()  {
		_data.reset(new TaskSchedulingData());
		_data->task = this;
#if ANVIL_DEBUG_TASKS
		_data->debug_id = g_task_debug_id++;
		{
			TaskDebugEvent e = TaskDebugEvent::CreateEvent(_data->debug_id, this);
			g_debug_event_handler(nullptr, &e);
		}
#endif
	}

	Task::~Task() {
		{
			const State s = GetState();
			if (s == STATE_SCHEDULED || s == STATE_EXECUTING || s == STATE_BLOCKED) {
				bool not_finished = true;
				while (not_finished) {
					// Wait for task to complete
					StrongSchedulingPtr data = _data;
					if (data) {
						std::shared_lock<std::shared_mutex> lock(data->lock);
						not_finished = data->wait_flag == 0u;
					} else {
						break;
					}
				}
			}

			_data->task = nullptr;
		}

#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::DestroyEvent(_data->debug_id);
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
		_data->exception = exception;
#endif
	}

	bool Task::Cancel() throw() {
		// If no scheduler is attached to this task then it cannot be canceled
		Scheduler* scheduler = _GetScheduler();
		if (scheduler == nullptr) return false;

		// Lock the scheduler's task queue
		bool notify = false;
		{
			std::lock_guard<std::shared_mutex> lock(scheduler->_task_queue_mutex);

			// If the state is not scheduled then it cannot be canceled
			if (GetState() != STATE_SCHEDULED) return false;

			// Remove the task from the queue
			for (auto i = scheduler->_task_queue.begin(); i < scheduler->_task_queue.end(); ++i) {
				if ((*i)->task == this) {
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
			std::lock_guard<std::shared_mutex> task_lock(_data->lock);
#if ANVIL_DEBUG_TASKS
			{
				TaskDebugEvent e = TaskDebugEvent::CancelEvent(_data->debug_id);
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
			std::lock_guard<std::shared_mutex> lock(_data->lock);
			_data->state = Task::STATE_CANCELED;
			_data->scheduler = nullptr;
			_data->wait_flag = 1u;
		}

		// Notify anythign waiting for changes to the task queue
		if (notify) scheduler->TaskQueueNotify();
		return true;
	}

	void Task::Wait() {
		Scheduler* scheduler = _GetScheduler();
		if (scheduler == nullptr || _data->state == Task::STATE_COMPLETE || _data->state == Task::STATE_CANCELED) return;

		const bool will_yield = scheduler->_no_execution_on_wait ?
			GetNumberOfTasksExecutingOnThisThread() > 0 :	// Only call yield if Wait is called from inside of a Task
			true;											// Always yield

#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::PauseEvent(_data->debug_id, will_yield);
			g_debug_event_handler(nullptr, &e);
		}
#endif

		const auto YieldCondition = [this]()->bool {
			std::shared_lock<std::shared_mutex> task_lock(_data->lock);
			if (_data->state != STATE_CANCELED && _data->state != STATE_COMPLETE) return false;
			return _data->wait_flag == 1u;
		};

		if (will_yield) {
			scheduler->Yield([this, YieldCondition]()->bool {
				return YieldCondition();
			});
		} else {
			while (! YieldCondition()) {
				// Wait for 1ms then check again
				std::unique_lock<std::mutex> lock(scheduler->_condition_mutex);
				if (YieldCondition()) break; // If the condition was met while acquiring the lock
				scheduler->_task_queue_update.wait_for(lock, std::chrono::milliseconds(1));
			}
		}

#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::ResumeEvent(_data->debug_id);
			g_debug_event_handler(nullptr, &e);
		}
#endif

#if ANVIL_TASK_HAS_EXCEPTIONS
		// Rethrow a caught exception
		if (_data->exception) {
			std::exception_ptr tmp = _data->exception;
			_data->exception = std::exception_ptr();
			std::rethrow_exception(tmp);
		}
#endif
	}


	void Task::SetPriority(Priority priority) {

		if constexpr(Priority::PRIORITY_HIGHEST > 255u) priority = static_cast<Priority>(priority +GetNestingDepth());

		std::exception_ptr exception = nullptr;
		Scheduler* scheduler = _GetScheduler();
		if (scheduler) {
			if (_data->state == STATE_SCHEDULED) {
				_data->priority = priority;
				std::lock_guard<std::shared_mutex> lock(scheduler->_task_queue_mutex);
				scheduler->SortTaskQueue();
			} else {
				exception = std::make_exception_ptr(std::runtime_error("Priority of a task cannot be changed when executing"));
				goto HANDLE_ERROR;
			}
		} else {
			_data->priority = priority;
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
			if (_data->state == STATE_COMPLETE || _data->state == STATE_CANCELED) {
				// Do nothing

			// If the exception was caught before or during execution
			} else {			
				// Cancel the Task
				_data->state = Task::STATE_CANCELED;
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
		} catch (std::exception&) {
			CatchException(std::move(std::current_exception()), false);
		}
#endif
		Scheduler::ThreadDebugData* debug_data = _data->scheduler->GetDebugDataForThread(g_thread_additional_data.scheduler_index);
		if (debug_data) {
			++debug_data->tasks_executing;
			++_data->scheduler->_scheduler_debug.total_tasks_executing;
		}

#if ANVIL_DEBUG_TASKS
		{
			TaskDebugEvent e = TaskDebugEvent::ExecuteBeginEvent(_data->debug_id);
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
			const auto CatchException = [&task](std::exception_ptr&& exception, bool set_exception) {
				// Handle the exception
				if (set_exception) task.SetException(std::move(exception));
				// If the exception was caught after the task finished execution
				if (task._data->state == STATE_COMPLETE || task._data->state == STATE_CANCELED) {
					// Do nothing

				// If the exception was caught before or during execution
				} else {
					// Cancel the Task
					task._data->state = Task::STATE_CANCELED;
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
			if (task._data->state != Task::STATE_CANCELED) {

				// Execute the task
				{
					std::lock_guard<std::shared_mutex> task_lock(task._data->lock);
					task._data->state = Task::STATE_EXECUTING;
				}
				try {
					task.OnExecution();
				} catch (std::exception&) {
					CatchException(std::move(std::current_exception()), true);
				} catch (...) {
					CatchException(std::exception_ptr(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception"))), true);
				}
			}

			try {
#if ANVIL_TASK_FIBERS
				fiber.task = nullptr;
#else
				g_thread_additional_data.task_stack.pop_back();
#endif
			} catch (std::exception&) {
				CatchException(std::move(std::current_exception()), false);
			}

			Scheduler::ThreadDebugData* debug_data = task._data->scheduler->GetDebugDataForThread(g_thread_additional_data.scheduler_index);
			if (debug_data) {
				--debug_data->tasks_executing;
				--task._data->scheduler->_scheduler_debug.total_tasks_executing;
			}

#if ANVIL_DEBUG_TASKS
			{
				TaskDebugEvent e = TaskDebugEvent::ExecuteEndEvent(task._data->debug_id);
				g_debug_event_handler(nullptr, &e);
			}
#endif

			{

				// Post-execution cleanup
				std::lock_guard<std::shared_mutex> task_lock(task._data->lock);

				task._data->state = Task::STATE_COMPLETE;
				task._data->wait_flag = 1u;
				task._data->scheduler = nullptr;
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

	void Scheduler::RemoveNextTaskFromQueue(StrongSchedulingPtr* tasks, uint32_t& count) throw() {
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
			std::lock_guard<std::shared_mutex> lock(_task_queue_mutex);

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
		StrongSchedulingPtr tasks[MAX_TASKS];
		uint32_t task_count = static_cast<uint32_t>(_task_queue.size()) / _scheduler_debug.total_thread_count;
		if (task_count < 1) task_count = 1u;
		if (task_count > MAX_TASKS) task_count = MAX_TASKS;
		RemoveNextTaskFromQueue(tasks, task_count);

		// If there is a task available then execute it
		if (task_count > 0) {

			for (uint32_t i = 0u; i < task_count; ++i) {
				tasks[i]->task->Execute(); 
				
				// Delete task data
				StrongSchedulingPtr tmp;
				tasks[i].swap(tmp);
			}

			return true;
		}

		return false;
	}


	void Scheduler::RegisterAsWorkerThread() {
		TaskThreadLocalData& local_data = g_thread_additional_data;
		local_data.is_worker_thread = true;

		{
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
		Task* const t = g_thread_additional_data.task_stack.empty() ? nullptr : g_thread_additional_data.task_stack.back();
#endif
		Scheduler::ThreadDebugData* const debug_data = GetDebugDataForThisThread();

#if ANVIL_DEBUG_TASKS
		const float debug_time = GetDebugTime();
#endif
		if (t) {
			std::lock_guard<std::shared_mutex> lock(t->_data->lock);

			// State change
			if (t->_data->state != Task::STATE_EXECUTING) throw std::runtime_error("anvil::Scheduler::Yield : Task cannot yield unless it is in STATE_EXECUTING");
			t->_data->state = Task::STATE_BLOCKED;
#if ANVIL_TASK_CALLBACKS
			t->OnBlock();
#endif
		}

#if ANVIL_TASK_FIBERS
		// Remember how the task should be resumed
		if (fiber) fiber->yield_condition = &condition;
#endif


		// While the condition is not met
		while (! condition()) {

			// If the thread is enabled
			bool thread_enabled = true;
			if (debug_data) thread_enabled = debug_data->enabled;

			// Try to execute a task
			if (thread_enabled && TryToExecuteTask()) {

			} else {
				const auto predicate = [this, &condition, thread_enabled]()->bool {
					if (thread_enabled) {
#if ANVIL_TASK_FIBERS
						if (g_thread_additional_data.AreAnyFibersReady()) return true;
#endif
						if (!_task_queue.empty()) return true;
					}
					return condition();
				};

				// Block until there is a queue update
				std::unique_lock<std::mutex> lock(_condition_mutex);

				// Check if something has changed while the mutex was being acquired
				if (!predicate()) {
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

					// Put the thread to sleep
					if (max_sleep_milliseconds == UINT32_MAX) { // Special behaviour, only wake when task updates happen (useful for implementing a thread pool)
						_task_queue_update.wait(lock);
					} else {
						_task_queue_update.wait_for(lock, std::chrono::milliseconds(max_sleep_milliseconds));
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

#if ANVIL_TASK_FIBERS
		// The task can no longer be resumed
		if(fiber) fiber->yield_condition = nullptr;
#endif

		// If this function is being called by a task
		if (t) {
			std::lock_guard<std::shared_mutex> lock(t->_data->lock);

			// State change
			t->_data->state = Task::STATE_EXECUTING;

#if ANVIL_TASK_CALLBACKS
			t->OnResume();
#endif
		}
	}

	void Scheduler::SortTaskQueue() throw() {
		std::sort(_task_queue.begin(), _task_queue.end(), [](const StrongSchedulingPtr& lhs, const StrongSchedulingPtr& rhs)->bool {
			return lhs->task->_data->priority < rhs->task->_data->priority;
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

#if ANVIL_TASK_FIBERS
		Task* const parent = g_thread_additional_data.current_fiber == nullptr ? nullptr : g_thread_additional_data.current_fiber->task;
#else
		Task* const parent = g_thread_additional_data.task_stack.empty() ? nullptr : g_thread_additional_data.task_stack.back();
#endif

		std::vector<StrongSchedulingPtr> task_data_tmp(count);

		// Initial error checking and initialisation
		size_t ready_count = 0u;
		for (uint32_t i = 0u; i < count; ++i) {
			Task& t = *tasks[i];

			std::lock_guard<std::shared_mutex> task_lock(t._data->lock);

			if (t._data->state != Task::STATE_INITIALISED) continue;

			StrongSchedulingPtr data(new TaskSchedulingData());
			task_data_tmp[i] = data;

			// Change state
			t._data->scheduled_flag = 1u;
			t._data->wait_flag = 0u;
			t._data->schedule_valid = 0u;
			t._data->state = Task::STATE_SCHEDULED;

			// Initialise scheduling data
			t._data = data;
			data->scheduler = this;
			data->task = &t;

#if ANVIL_TASK_HAS_EXCEPTIONS
			t._data->exception = std::exception_ptr();
#endif

			// Update the child / parent relationship between tasks
#if ANVIL_USE_PARENTCHILDREN
			if (parent) {
				std::lock_guard<std::shared_mutex> parent_lock(parent->_data->lock);
				StrongSchedulingPtr parent_data = parent->_data;
				data->parent = parent_data;
				parent_data->children.push_back(data);
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

			// Skip the task if initalisation failed
			if (data->scheduler != this_scheduler) continue;
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
				if (parent) parent_id = parent->_data->debug_id;
				TaskDebugEvent e = TaskDebugEvent::ScheduleEvent(t._data->debug_id, parent_id, _debug_id);
				g_debug_event_handler(nullptr, &e);
			}
#endif
			t._data->schedule_valid = 1u;
			++ready_count;
		}

		if (ready_count > 0u) {
			{
				// Lock the task queue
				std::lock_guard<std::shared_mutex> lock(_task_queue_mutex);
				_task_queue.reserve(_task_queue.size() + ready_count);
				for (uint32_t i = 0u; i < count; ++i) {
					if (tasks[i]->_data->schedule_valid) {
						// Add to the active queue
						_task_queue.push_back(task_data_tmp[i]);
					}
				}
				// Sort task list by priority
				SortTaskQueue();
			}

			// Notify waiting threads
			TaskQueueNotify();
		}
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