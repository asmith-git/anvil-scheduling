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
#endif
#include <string>
#include <atomic>
#include <algorithm>
#include <list>
#include "asmith/TaskScheduler/Core.hpp"
#include "asmith/TaskScheduler/Scheduler.hpp"
#include "asmith/TaskScheduler/Task.hpp"

namespace anvil {

	struct TaskThreadLocalData {
		std::shared_ptr<Task> task;
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
		std::shared_ptr<Task> task;

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
			_tasks.push_back(TaskThreadLocalData());
			TaskThreadLocalData* data = &_tasks.back();
			data->task = task.shared_from_this();
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
#if ANVIL_DEBUG_TASKS
			TaskThreadLocalData* data = GetCurrentExecutingTaskData();
			if (data == nullptr || data->task.get() != &task) throw std::runtime_error("anvil::_TaskThreadLocalData::OnTaskExecuteEnd : Task is not the currently executing one");
#endif
			{
				auto end = _tasks_by_priority.end();
				auto i = std::find_if(_tasks_by_priority.begin(), end, [&task](const TaskThreadLocalData* data)->bool {
					return data->task.get() == &task;
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
					return data.task.get() == &task;
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
		}

		inline TaskThreadLocalData* GetCurrentExecutingTaskData() {
			return _current_task;
		}

		TaskThreadLocalData* GetTaskData(Task& task) {
			for (TaskThreadLocalData& data : _tasks) {
				if (data.task.get() == &task) return &data;
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
		return task == nullptr ? "NULL" : std::to_string(task->GetDebugID());
	}

	static std::string GetShortName(const Scheduler* scheduler) {
		return scheduler == nullptr ? "NULL" : std::to_string(reinterpret_cast<uintptr_t>(scheduler));
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

		// Replace scheduler name
		i = message.find("%scheduler%");
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


		// Replace thread id
		i = message.find("%thread%");
		if (i != std::string::npos) {
			std::string thread = (g_thread_local_data.is_worker_thread ? "WORKER_" : "USER_") + (std::ostringstream() << std::this_thread::get_id()).str();
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
#endif

#define INVALID_SCHEDULER nullptr

	// Task
#if ANVIL_DEBUG_TASKS
	static std::atomic_uint64_t g_debug_id = 0u;
#endif

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
		_debug_id = g_debug_id++;
#endif
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

	std::mutex& Task::GetMutex() const {
		Scheduler* scheduler = _scheduler;
		if (scheduler) {
			return scheduler->_mutex;
		} else {
			static std::mutex g_task_mutex;
			return g_task_mutex;
		}
	}

	std::shared_ptr<Task> Task::GetCurrentlyExecutingTask() {
		auto data = g_thread_local_data.GetCurrentExecutingTaskData();
		return data == nullptr ? nullptr : data->task;
	}

	size_t Task::GetNumberOfTasksExecutingOnThisThread() {
		return g_thread_local_data.GetNumberOfTasks();
	}

	std::shared_ptr<Task> Task::GetCurrentlyExecutingTask(size_t index) {
		auto data = g_thread_local_data.GetTaskData(index);
		return data == nullptr ? nullptr : data->task;
	}

	void Task::Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds) {
		Scheduler* scheduler = _scheduler;
		if (scheduler) {
			scheduler->Yield(condition, max_sleep_milliseconds);
		} else {
			throw std::runtime_error("anvil::Task::Yield : Cannot yield without a scheduler");
		}
	}


	void Task::SetException(std::exception_ptr exception) {
#if ANVIL_TASK_HAS_EXCEPTIONS
		_exception = exception;
#endif
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(this, nullptr, "Caught exception in task %task% on thread %thread%");
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
				if (i->get() == this) {
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
			} catch (std::exception&) {
				SetException(std::current_exception());
			} catch (...) {
				SetException(std::make_exception_ptr(std::runtime_error("Thrown value was not a C++ exception")));
			}
			
#endif
			// State change and cleanup
			_state = Task::STATE_CANCELED;
			_scheduler = INVALID_SCHEDULER;
		}

		// Canceled successfully
		_wait_flag = 1u;

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
		const float time = GetDebugTime();
		anvil::PrintDebugMessage(this, nullptr, 
			will_yield ?  "Waiting on thread %thread% for Task %task% to complete execution" :
			"Waiting on thread %thread% for Task %task% to complete execution without yielding"
		);
#endif

		#define YieldCondition() (_wait_flag == 1)

		if (will_yield) {
			scheduler->Yield([this]()->bool {
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

	Scheduler* Task::_GetScheduler() const throw() {
		return _scheduler;
	}

	void Task::Execute() throw() {
#if ANVIL_DEBUG_TASKS
		{
			const float time = GetDebugTime();
			anvil::PrintDebugMessage(this, nullptr, "Task %task% begins execution on thread %thread% after being scheduled for " + std::to_string(time - _debug_timer) + " milliseconds");
			_debug_timer = time;
		}
#endif
		// Remember the scheduler for later
		Scheduler* const scheduler = _GetScheduler();

		const auto CatchException = [this](std::exception_ptr&& exception, bool set_exception) {
			// Handle the exception
#if ANVIL_TASK_HAS_EXCEPTIONS
			if (set_exception) this->SetException(std::move(exception));
#endif

#if ANVIL_DEBUG_TASKS
			{
				std::string message = "Task %task% threw exception ";
				switch (_state) {
				case STATE_SCHEDULED:
					message += "before starting execution";
					break;
				case STATE_EXECUTING:
					message += "while executing for ";
APPEND_TIME:
					message += std::to_string(GetDebugTime() - _debug_timer) + " milliseconds";
					break;
				case STATE_COMPLETE:
					message += "after completing execution, which lasted ";
					goto APPEND_TIME;
				case STATE_CANCELED:
					message += "after being canceled";
					break;
				default:
					message += "in an undefined state";
					break;
				};

				message += " on thread %thread%";

				anvil::PrintDebugMessage(this, nullptr, message.c_str());
			}
#endif
			// If the exception was caught after the task finished execution
			if (_state == STATE_COMPLETE || _state == STATE_CANCELED) {
				// Do nothing

			// If the exception was caught before or during execution
			} else {			
				// Cancel the Task
				_state = Task::STATE_CANCELED;
#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(this, nullptr, "Task %task% canceled due to an error");
#endif
#if ANVIL_TASK_CALLBACKS
				// Call the cancelation callback
				try {
					OnCancel();
				} catch (std::exception& e) {
#if ANVIL_DEBUG_TASKS
					anvil::PrintDebugMessage(this, nullptr, std::string("Caught exception on thread %thread% : ") + e.what());
#endif
#if ANVIL_TASK_HAS_EXCEPTIONS
					// Task caught during execution takes priority as it probably has more useful debugging information
					if (!set_exception) this->SetException(std::current_exception());
#endif
				} catch (...) {
#if ANVIL_DEBUG_TASKS
					anvil::PrintDebugMessage(this, nullptr, "Caught non-C++ exception on thread %thread%");
#endif
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
#if ANVIL_DEBUG_TASKS
			anvil::PrintDebugMessage(this, nullptr, std::string("Caught exception on thread %thread% : ") + e.what());
#endif
			CatchException(std::move(std::current_exception()), false);
		}

		// Switch control to the task's fiber
#if ANVIL_TASK_FIBERS
		g_thread_local_data.SwitchToTask(*g_thread_local_data.GetTaskData(*this));
#else
		FiberData data;
		data.task = this->shared_from_this();
		FiberFunction(&data);
#endif
	}

	std::shared_ptr<Task> Task::GetParent() const throw() {
#if ANVIL_TASK_PARENT || ANVIL_TASK_FAST_CHILD_COUNT
		return _parent;
#endif
		return nullptr;
	}

	std::vector<std::shared_ptr<Task>> Task::GetChildren() const throw() {
		std::vector<std::shared_ptr<Task>> children;
#if ANVIL_TASK_PARENT
		{
			std::lock_guard<std::mutex> lock(GetMutex());
			for (const std::weak_ptr<Task>& t : _children) {
				std::shared_ptr<Task> t2 = t.lock();
				if (t2) children.push_back(std::move(t2));
			}
		}
#endif
		return children;
	}

	size_t Task::GetChildCount(bool aproximate) const throw() {
#if ANVIL_TASK_PARENT
		if (aproximate) {
			return _fast_child_count;
		} else {
			size_t count = 0u;
			std::lock_guard<std::mutex> lock(GetMutex());
			for (const std::weak_ptr<Task>& t : _children) {
				std::shared_ptr<Task> t2 = t.lock();
				if (t2) ++count;
			}
			return count;
		}
#elif ANVIL_TASK_FAST_CHILD_COUNT
		return _fast_child_count;
#else
		return 0u;
#endif
	}

	size_t Task::GetRecursiveChildCount(bool aproximate) const throw() {
#if ANVIL_TASK_PARENT
		if (aproximate) {
			return _fast_recursive_child_count;
		} else {
			size_t count = 0;
			std::vector<std::shared_ptr<Task>> children = GetChildren();
			for (std::shared_ptr<Task>& child : children) count += child->GetRecursiveChildCount(aproximate);
			return count + children.size();
		}
#elif ANVIL_TASK_FAST_CHILD_COUNT
		return _fast_recursive_child_count;
#else
		return 0u;
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

		#if ANVIL_DEBUG_TASKS
					{
						std::string message = "Task %task% threw exception ";
						switch (task._state) {
						case STATE_SCHEDULED:
							message += "before starting execution";
							break;
						case STATE_EXECUTING:
							message += "while executing for ";
						APPEND_TIME:
							message += std::to_string(GetDebugTime() - task._debug_timer) + " milliseconds";
							break;
						case STATE_COMPLETE:
							message += "after completing execution, which lasted ";
							goto APPEND_TIME;
						case STATE_CANCELED:
							message += "after being canceled";
							break;
						default:
							message += "in an undefined state";
							break;
						};

						message += " on thread %thread%";

						anvil::PrintDebugMessage(&task, nullptr, message.c_str());
					}
		#endif
					// If the exception was caught after the task finished execution
					if (task._state == STATE_COMPLETE || task._state == STATE_CANCELED) {
						// Do nothing

					// If the exception was caught before or during execution
					}
					else {
						// Cancel the Task
						task._state = Task::STATE_CANCELED;
		#if ANVIL_DEBUG_TASKS
						anvil::PrintDebugMessage(&task, nullptr, "Task %task% canceled due to an error");
		#endif
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

		#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(&task, nullptr, "Task %task% finishes execution on thread %thread% after " + std::to_string(GetDebugTime() - task._debug_timer) + " milliseconds");
		#endif

				try {
					g_thread_local_data.OnTaskExecuteEnd(task);
				}
				catch (std::exception& e) {
					CatchException(std::move(std::current_exception()), false);
				}
				catch (...) {
					CatchException(std::exception_ptr(), false);
				}

				task._wait_flag = 1;

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
		anvil::PrintDebugMessage(nullptr, this, "Scheduler %scheduler% has been created on thread %thread%");
#endif
	}

	Scheduler::~Scheduler() {
		//! \bug Scheduled tasks are left in an undefined state
#if ANVIL_DEBUG_TASKS
		anvil::PrintDebugMessage(nullptr, this, "Scheduler %scheduler% has been destroyed on thread %thread%");
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

	void Scheduler::RemoveNextTaskFromQueue(std::shared_ptr<Task>* tasks, uint32_t& count) throw() {
#if ANVIL_TASK_DELAY_SCHEDULING
		// Check if there are tasks before locking the queue
		// Avoids overhead of locking during periods of low activity
		if (_task_queue.empty()) {
			// If there are no active tasks, check if an innactive one has now become ready
			if (_unready_task_queue.empty()) return std::shared_ptr<Task>();

			std::lock_guard<std::mutex> lock(_mutex);
			CheckUnreadyTasks();

			if (_task_queue.empty()) {
				count = 0u;
				return;
			}
		}

		std::shared_ptr<Task> task = nullptr;
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
		tasks[0u].swap(task);
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
				tasks[count2++].swap(_task_queue.back());
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
		std::shared_ptr<Task> tasks[MAX_TASKS];
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
#if ANVIL_DEBUG_TASKS
			anvil::PrintDebugMessage(&t, nullptr, "Task %task% yielding on thread %thread% after executing for " + std::to_string(debug_time - t._debug_timer) + " milliseconds");
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
						anvil::PrintDebugMessage(nullptr, this, "Thread %thread% put to sleep because there was no work on Scheduler %scheduler%");
						const float sleep_start_time = GetDebugTime();
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
						anvil::PrintDebugMessage(nullptr, this, "Thread %thread% woke up after " + std::to_string(GetDebugTime() - sleep_start_time) + " milliseconds");
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
#if ANVIL_DEBUG_TASKS
			anvil::PrintDebugMessage(&t, nullptr, "Task %task% resumed execution on thread %thread% after yielding for " + std::to_string(GetDebugTime() - debug_time) + " milliseconds");
#endif

#if ANVIL_TASK_CALLBACKS
			t.OnResume();
#endif
		}
	}

	void Scheduler::SortTaskQueue() throw() {
		std::sort(_task_queue.begin(), _task_queue.end(), [](const std::shared_ptr<Task>& lhs, const std::shared_ptr<Task>& rhs)->bool {
			return lhs->_priority < rhs->_priority;
		});
	}

	void Scheduler::Schedule(std::shared_ptr<Task>* tasks, const uint32_t count) {
		// Schedule in smaller groups so tasks can start executing as they are scheduled
		//{
		//	uint32_t block_size = _task_queue.empty() ? _scheduler_debug.total_thread_count.load() : 256;

		//	if (count > block_size) {
		//		uint32_t count2 = count;
		//		while (count2 > 0) {
		//			uint32_t tasks_to_add = count2 < block_size ? count2 : block_size;
		//			Schedule(tasks, tasks_to_add);
		//			tasks += tasks_to_add;
		//			count2 -= tasks_to_add;

		//			block_size = 256;
		//		}
		//		return;
		//	}
		//}

		const auto this_scheduler = this;

#if ANVIL_TASK_PARENT || ANVIL_TASK_FAST_CHILD_COUNT
		TaskThreadLocalData* parent_local = g_thread_local_data.GetCurrentExecutingTaskData();
		std::shared_ptr<Task> const parent = parent_local ? parent_local->task : nullptr;
#endif

		// Initial error checking and initialisation
		for (uint32_t i = 0u; i < count; ++i) {
			Task& t = *tasks[i];

			if (t._state != Task::STATE_INITIALISED) {
#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(&t, this, "Task (%task%) cannot be scheduled unless it is in STATE_INITIALISED");
#endif
				continue;
			}

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
#if ANVIL_DEBUG_TASKS
				anvil::PrintDebugMessage(&t, this, "Task %task% is a child of Task " + std::to_string(parent->GetDebugID()));
#endif
#if ANVIL_TASK_PARENT
				parent->_children.push_back(tasks[i]);
#endif
				++t._nesting_depth;
				++parent->_fast_child_count;

				std::shared_ptr<anvil::Task> parent2 = parent;
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
	
	void Scheduler::Schedule(std::shared_ptr<Task> task, Priority priority) {
		task->SetPriority(priority);
		Schedule(task);
	}

#if ANVIL_TASK_EXTENDED_PRIORITY
	void Scheduler::RecalculatedExtendedPriorities() {
		std::lock_guard<std::mutex> lock(_mutex);
		for (std::shared_ptr<Task>& t : _task_queue) {
			t->_priority.extended = t->CalculateExtendedPriorty();
		}
		SortTaskQueue();
	}
#endif

	uint32_t Scheduler::GetThisThreadIndex() const {
		return g_thread_local_data.scheduler_index;
	}

	Scheduler::ThreadDebugData* Scheduler::GetDebugDataForThread(const uint32_t index) {
		return index > _scheduler_debug.total_thread_count ? nullptr : _scheduler_debug.thread_debug_data + index;
	}

	Scheduler::SchedulerDebugData& Scheduler::GetDebugData() {
		_scheduler_debug.sleeping_thread_count = _scheduler_debug.total_thread_count - _scheduler_debug.executing_thread_count;
		_scheduler_debug.total_tasks_queued = static_cast<uint32_t>(_task_queue.size());
		return _scheduler_debug;
	}
}