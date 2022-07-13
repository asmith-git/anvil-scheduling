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
#include <list>
#include "asmith/TaskScheduler/Core.hpp"
#include "asmith/TaskScheduler/Scheduler.hpp"
#include "asmith/TaskScheduler/Task.hpp"

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
			float schedule_time;
			float execution_start_time;
			float pause_time;
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
			ss << scheduler_event->time << " ms : ";

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
				ss << "Task " << task_event->task_id << " was created, type is " << GetLongName(task_event->task);
				break;

			case TaskDebugEvent::EVENT_SCHEDULE:
				g_task_debug_data.emplace(task_event->task_id, TaskDebugData{ task_event->time, 0.f, 0.f });
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
					debug.pause_time = task_event->time;

					ss << "Task " << task_event->task_id << " completes execution after " << (task_event->time - debug.execution_start_time) << " ms";
				}
				break;

			case TaskDebugEvent::EVENT_DESTROY:
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

	struct TaskThreadLocalData {
		Task* task;
		const std::function<bool(void)>* yield_condition;

		TaskThreadLocalData() :
			task(nullptr),
			yield_condition(nullptr)
		{}
	};

	struct FiberData {
#if ANVIL_TASK_FIBERS
		LPVOID fiber;
#else
		void* fiber;
#endif
		Task* task;

		FiberData() :
			fiber(nullptr),
			task(nullptr)
		{}
	};

	class _TaskThreadLocalData {
	private:
		enum { MAX_NESTED_TASKS = 256u };

#if ANVIL_TASK_FIBERS
		LPVOID _fiber;
#endif
		std::list<FiberData> _fibers;
		std::list<TaskThreadLocalData> _tasks;
		std::vector<TaskThreadLocalData*> _tasks_by_priority;
		TaskThreadLocalData* _current_task;

		FiberData* AllocateFiber() {
			// For for an unused fiber
			for (FiberData& fiber : _fibers) {
				if (fiber.task == nullptr) {
					return &fiber;
				}
			}

			// Allocate a new fiber
			_fibers.push_back(std::move(FiberData()));
			FiberData* fiber = &_fibers.back();
#if ANVIL_TASK_FIBERS
			fiber->fiber = CreateFiber(0u, Task::FiberFunction, fiber);
#endif
			return fiber;
		}

		void DeallocateFiber(FiberData* fiber) {
#if ANVIL_TASK_FIBERS
			fiber->task = nullptr;
#endif
		}
	public:
		uint32_t scheduler_index;
		bool is_worker_thread;

		_TaskThreadLocalData() :
#if ANVIL_TASK_FIBERS
			_fiber(nullptr),
#endif
			_current_task(nullptr),
			scheduler_index(UINT32_MAX),
			is_worker_thread(false)
		{
#if ANVIL_TASK_FIBERS
			_fiber = ConvertThreadToFiber(nullptr);
#endif
		}

		~_TaskThreadLocalData() {
#if ANVIL_TASK_FIBERS
			// Delete old fibers
			for (FiberData& fiber : _fibers) {
				DeleteFiber(fiber.fiber);
			}
#endif
			_fibers.clear();
		}

		void SwitchToMainFiber() {
			// Are we currently executing the main fiber?
			if (_current_task == nullptr) return;

			// Switch to it
#if ANVIL_TASK_FIBERS
			_current_task = nullptr;
			SwitchToFiber(_fiber);
#endif
		}

#if ANVIL_TASK_FIBERS
		bool SwitchToTask(TaskThreadLocalData& task) {
			if (&task == _current_task) return false;

			// If the task is able to execute
			if (task.yield_condition == nullptr || (*task.yield_condition)()) {
				_current_task = &task;
				SwitchToFiber(task.task->_fiber);
				return true;
			}

			return false;
		}

		bool SwitchToAnyTask() {
			// Try to execute a task that is ready to resume
			for (TaskThreadLocalData* t : _tasks_by_priority) {
				if (SwitchToTask(*t)) return true;
			}

			return false;
		}
#endif

		inline void OnTaskExecuteBegin(Task& task) {
#if ANVIL_DEBUG_TASKS
			{
				TaskDebugEvent e = TaskDebugEvent::ExecuteBeginEvent(task._debug_id);
				g_debug_event_handler(nullptr, &e);
			}
#endif
			_tasks.push_back(TaskThreadLocalData());
			TaskThreadLocalData* data = &_tasks.back();
			data->task = &task;
			_tasks_by_priority.push_back(data);

			// Sort tasks by priority
			std::sort(_tasks_by_priority.begin(), _tasks_by_priority.end(), [](const TaskThreadLocalData* lhs, const TaskThreadLocalData* rhs)->bool {
				return lhs->task->_priority < rhs->task->_priority;
			});

#if ANVIL_TASK_FIBERS
			FiberData* fiber = AllocateFiber();
			fiber->task = data->task;
			task._fiber = fiber->fiber;
#else
			_current_task = data;
#endif
		}

		void OnTaskExecuteEnd(Task& task) {
			{
				auto end = _tasks_by_priority.end();
				auto i = std::find_if(_tasks_by_priority.begin(), end, [&task](const TaskThreadLocalData* data)->bool {
					return data->task == &task;
				});
				if (i != end) {
					if (_tasks_by_priority.size() == 1u) {
						_tasks_by_priority.clear();
					} else {
						_tasks_by_priority.erase(i);
					}
				}
			}
			{
				auto end = _tasks.end();
				auto i = std::find_if(_tasks.begin(), end, [&task](const TaskThreadLocalData& data)->bool {
					return data.task == &task;
				});
				if (i != end) {
					if (_tasks.size() == 1u) {
						_tasks.clear();
					} else {
						_tasks.erase(i);
					}
				}
			}

#if ANVIL_TASK_FIBERS
			for (FiberData& fiber : _fibers) {
				if (fiber.fiber == task._fiber) {
					DeallocateFiber(&fiber);
					break;
				}
			}
			task._fiber = nullptr;
#else
			if (_tasks.empty()) {
				_current_task = nullptr;
			} else {
				_current_task = &_tasks.back();
			}

#endif
#if ANVIL_DEBUG_TASKS
			{
				TaskDebugEvent e = TaskDebugEvent::ExecuteEndEvent(task._debug_id);
				g_debug_event_handler(nullptr, &e);
			}
#endif
		}

		inline TaskThreadLocalData* GetCurrentExecutingTaskData() {
			return _current_task;
		}

		TaskThreadLocalData* GetTaskData(Task& task) {
			for (TaskThreadLocalData& data : _tasks) {
				if (data.task == &task) return &data;
			}

			return nullptr;
		}

		inline TaskThreadLocalData* GetTaskData(size_t index) {
			auto i = _tasks.begin();
			while (index > 0) {
				--index;
				++i;
			}
			return &*i;
		}

		inline size_t GetNumberOfTasks() const {
			return _tasks.size();
		}
	};

	thread_local _TaskThreadLocalData g_thread_local_data;

#if ANVIL_USE_NEST_COUNTER
	thread_local int32_t g_tasks_nested_on_this_thread = 0; //!< Tracks if Task::Execute is being called on this thread (and how many tasks are nested)
#endif

#if ANVIL_DEBUG_TASKS

	static float GetDebugTime() {
		static const uint64_t g_reference_time = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		return static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - g_reference_time) / 1000000.f;
	}

	//static std::string GetShortName(const Task* task) {
	//	return task == nullptr ? "NULL" : std::to_string(task->GetDebugID());
	//}

	//static std::string GetShortName(const Scheduler* scheduler) {
	//	return scheduler == nullptr ? "NULL" : std::to_string(reinterpret_cast<uintptr_t>(scheduler));
	//}



	//static std::string GetLongName(const Scheduler* scheduler) {
	//	return FormatClassName(typeid(*scheduler).name());
	//}


	//static void _PrintDebugMessage(std::string message) {
	//}

	//static void PrintDebugMessage(const Task* task, const Scheduler* scheduler, std::string message) {
	//	// Replace task name
	//	auto i = message.find("%task%");
	//	if (i != std::string::npos) {
	//		std::string short_name = GetShortName(task);
	//		while (i != std::string::npos) {
	//			message.replace(i, 6, short_name);
	//			i = message.find("%task%");
	//		}
	//	}

	//	i = message.find("%task_class%");
	//	if (i != std::string::npos) {
	//		std::string long_name = GetLongName(task);
	//		while (i != std::string::npos) {
	//			message.replace(i, 12, long_name);
	//			i = message.find("%task_class%");
	//		}
	//	}

	//	// Replace scheduler name
	//	i = message.find("%scheduler%");
	//	if (i != std::string::npos) {
	//		std::string short_name = GetShortName(scheduler);
	//		while (i != std::string::npos) {
	//			message.replace(i, 11, short_name);
	//			i = message.find("%scheduler%");
	//		}
	//	}

	//	i = message.find("%scheduler_class%");
	//	if (i != std::string::npos) {
	//		std::string long_name = GetLongName(scheduler);
	//		while (i != std::string::npos) {
	//			message.replace(i, 17, long_name);
	//			i = message.find("%scheduler_class%");
	//		}
	//	}


	//	// Replace thread id
	//	i = message.find("%thread%");
	//	if (i != std::string::npos) {
	//		std::string thread = (g_thread_local_data.is_worker_thread ? "WORKER_" : "USER_") + (std::ostringstream() << std::this_thread::get_id()).str();
	//		message.replace(i, 8, thread);
	//	}

	//	// Append line break
	//	if (message.back() != '\n') message += '\n';

	//	// Timestamp
	//	message = std::to_string(GetDebugTime()) + "ms : " + message;

	//	// Write the message
	//	static std::mutex g_lock;
	//	std::lock_guard<std::mutex> locl(g_lock);
	//	g_debug_stream->write(message.c_str(), message.size());
	//}

	//void Task::PrintDebugMessage(const char* message) const {
	//	anvil::PrintDebugMessage(this, nullptr, message);
	//}

	//void Scheduler::PrintDebugMessage(const char* message) const {
	//	anvil::PrintDebugMessage(nullptr, this, message);
	//}

	static std::atomic_uint32_t g_task_debug_id = 0u;
	static std::atomic_uint32_t g_scheduler_debug_id = 0u;
#endif

#define INVALID_SCHEDULER nullptr

	// Task

	Task::Task() :
		_scheduler(INVALID_SCHEDULER),
#if ANVIL_TASK_FIBERS
		_fiber(nullptr),
#endif
#if ANVIL_TASK_FAST_CHILD_COUNT || ANVIL_TASK_PARENT
		_fast_child_count(0u),
		_fast_recursive_child_count(0u),
		_nesting_depth(0u),
#endif
		_wait_flag(0u),
		_priority(Priority::PRIORITY_MIDDLE),
		_state(STATE_INITIALISED)
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
#if ANVIL_TASK_FAST_CHILD_COUNT || ANVIL_TASK_PARENT
		// Make sure children are destroyed first
		while (_fast_child_count + _fast_recursive_child_count > 0u);

		// Remove task from list of children
		Task* parent = _parent;
		if (parent) {
#if ANVIL_TASK_PARENT
			std::lock_guard<std::mutex> lock(parent->GetMutex());
			auto end = parent->_children.end();
			auto i = std::find(parent->_children.begin(), end, this);
			_parent->_children.erase(i);
#endif

			--_parent->_fast_child_count;

			while (parent) {
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

	std::mutex& Task::GetMutex() const {
		Scheduler* scheduler = _scheduler;
		if (scheduler) {
			return scheduler->_mutex;
		} else {
			static std::mutex g_task_mutex;
			return g_task_mutex;
		}
	}

	Task* Task::GetCurrentlyExecutingTask() {
		auto data = g_thread_local_data.GetCurrentExecutingTaskData();
		return data == nullptr ? nullptr : data->task;
	}

	size_t Task::GetNumberOfTasksExecutingOnThisThread() {
		return g_thread_local_data.GetNumberOfTasks();
	}

	Task* Task::GetCurrentlyExecutingTask(size_t index) {
		auto data = g_thread_local_data.GetTaskData(index);
		return data == nullptr ? nullptr : data->task;
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
		}

		// Canceled successfully
		try {
			uint8_t expected = 0u;
			if (!_wait_flag.compare_exchange_strong(expected, 1u, std::memory_order_acq_rel)) throw std::runtime_error("Task::Cancel : Memory anomaly detected on wait flag");
		} catch (...) {
			SetException(std::current_exception());
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
			uint8_t expected = 1u;
			return _wait_flag.compare_exchange_strong(expected, 2u, std::memory_order_acq_rel);
		};

		if (will_yield) {
			scheduler->Yield([this, YieldCondition]()->bool {
				return YieldCondition();
			});
		} else {
			while (! YieldCondition()) {
				// Wait for 1ms then check again
				std::unique_lock<std::mutex> lock(scheduler->_mutex);
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

		try {
			g_thread_local_data.OnTaskExecuteBegin(*this);
		} catch (std::exception& e) {
			CatchException(std::move(std::current_exception()), false);
		}

		// Switch control to the task's fiber
#if ANVIL_TASK_FIBERS
		g_thread_local_data.SwitchToTask(*g_thread_local_data.GetTaskData(*this));
#else
		FiberData data;
		data.task = this;
		FiberFunction(&data);
#endif
	}

#if ANVIL_TASK_FIBERS
	void WINAPI Task::FiberFunction(LPVOID param) {
		while (true) {
#else
	void Task::FiberFunction(void* param) {
		if (true) {
#endif
			{
				FiberData& fibData = *static_cast<FiberData*>(param);
				Task& task = *fibData.task;


				const auto CatchException = [&task](std::exception_ptr&& exception, bool set_exception) {
					// Handle the exception
					if (set_exception) task.SetException(std::move(exception));
					// If the exception was caught after the task finished execution
					if (task._state == STATE_COMPLETE || task._state == STATE_CANCELED) {
						// Do nothing

					// If the exception was caught before or during execution
					}
					else {
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

				Scheduler& scheduler = task.GetScheduler();

				// If an error hasn't been detected yet
				if (task._state != Task::STATE_CANCELED) {

					// Execute the task
					task._state = Task::STATE_EXECUTING;
					try {
						task.OnExecution();
						task._state = Task::STATE_COMPLETE;
					}
					catch (std::exception& e) {
						CatchException(std::move(std::current_exception()), true);
					} catch (...) {
						CatchException(std::exception_ptr(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception"))), true);
					}
				}

				// Post-execution cleanup
				task._scheduler = INVALID_SCHEDULER;

				try {
					g_thread_local_data.OnTaskExecuteEnd(task);
				} catch (std::exception& e) {
					CatchException(std::move(std::current_exception()), false);
				}

				try {
					uint8_t expected = 0u;
					if (!task._wait_flag.compare_exchange_strong(expected, 1u, std::memory_order_acq_rel)) throw std::runtime_error("Task::Execute : Memory anomaly detected on write flag");
				} catch (...) {
					CatchException(std::move(std::current_exception()), false);
				}


				scheduler.TaskQueueNotify();
			}

			// Return control to the main thread
			g_thread_local_data.SwitchToMainFiber();
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
		if(g_thread_local_data.SwitchToAnyTask()) return true;
#endif

		// Try to start the execution of a new task
		enum { MAX_TASKS = 32u };
		Task* tasks[MAX_TASKS];
		uint32_t task_count = static_cast<uint32_t>(_task_queue.size()) / _scheduler_debug.total_thread_count;
		if (task_count < 1) task_count = 1u;
		if (task_count > MAX_TASKS) task_count = MAX_TASKS;
		RemoveNextTaskFromQueue(tasks, task_count);

		// If there is a task available then execute it
		if (task_count > 0) {
			_TaskThreadLocalData& local_data = g_thread_local_data;
			ThreadDebugData* debug_data = GetDebugDataForThread(local_data.scheduler_index);
			if (debug_data) {
				debug_data->tasks_executing += task_count;
				_scheduler_debug.total_tasks_executing += task_count;
			}

			for (uint32_t i = 0u; i < task_count; ++i) {
				tasks[i]->Execute();
			}

			if (debug_data) {
				debug_data->tasks_executing -= task_count;
				_scheduler_debug.total_tasks_executing -= task_count;
			}

			return true;
		}

		return false;
	}


	void Scheduler::RegisterAsWorkerThread() {
		_TaskThreadLocalData& local_data = g_thread_local_data;
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
		TaskThreadLocalData* data = g_thread_local_data.GetCurrentExecutingTaskData();
		Scheduler::ThreadDebugData* debug_data = GetDebugDataForThisThread();

#if ANVIL_DEBUG_TASKS
		const float debug_time = GetDebugTime();
#endif
		if (data) {
			Task& t = *data->task;

			// State change
			if (t._state != Task::STATE_EXECUTING) throw std::runtime_error("anvil::Scheduler::Yield : Task cannot yield unless it is in STATE_EXECUTING");
			t._state = Task::STATE_BLOCKED;
#if ANVIL_TASK_CALLBACKS
			t.OnBlock();
#endif


			// Remember how the task should be resumed
			data->yield_condition = &condition;
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

					// Acquire a lock on the scheduler
					const size_t tasks_queued_before_lock = _task_queue.size();
					std::unique_lock<std::mutex> lock(_mutex);

					// Check if the condition was met while acquiring the lock
					if (condition()) goto EXIT_CONDITION;

					// Check if a task was added while acquiring the lock
					if (!_task_queue.empty()) continue;

					// If the number of queued tasks has changed (becuase the lock took time to acquire) 
					// Then skip over the sleep for this attempt
					if (_task_queue.size() == tasks_queued_before_lock) {
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
		}

		// If this function is being called by a task
		if (data) {
			Task& t = *data->task;

			// The task can no longer be resumed
			data->yield_condition = nullptr;

			// State change
			t._state = Task::STATE_EXECUTING;

#if ANVIL_TASK_CALLBACKS
			t.OnResume();
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
		TaskThreadLocalData* parent_local = g_thread_local_data.GetCurrentExecutingTaskData();
		Task* const parent = parent_local ? parent_local->task : nullptr;
#endif

		// Initial error checking and initialisation
		for (uint32_t i = 0u; i < count; ++i) {
			Task& t = *tasks[i];

			if (t._state != Task::STATE_INITIALISED) continue;

			// Change state
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
#if ANVIL_TASK_PARENT
				parent->_children.push_back(tasks[i]);
#endif
				++t._nesting_depth;
				++parent->_fast_child_count;

				anvil::Task* parent2 = parent;
				while (parent2) {
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
		return g_thread_local_data.scheduler_index;
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
		if (handler = nullptr) handler = DefaultEventHandler;
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