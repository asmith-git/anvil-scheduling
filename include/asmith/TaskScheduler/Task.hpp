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

#ifndef ANVIL_SCHEDULER_TASK_HPP
#define ANVIL_SCHEDULER_TASK_HPP

#include <atomic>
#include <stdexcept>
#include <memory>
#include <shared_mutex>
#include "asmith/TaskScheduler/Scheduler.hpp"

#if ANVIL_TASK_FIBERS
	#define NOMINMAX
	#include <windows.h>
	#undef Yield
#endif

#define ANVIL_USE_PARENTCHILDREN (ANVIL_TASK_PARENT || ANVIL_TASK_FAST_CHILD_COUNT)

namespace anvil {
#if ANVIL_DEBUG_TASKS
	struct ANVIL_DLL_EXPORT SchedulerDebugEvent {
		enum Type : uint8_t {
			EVENT_CREATE,
			EVENT_PAUSE,
			EVENT_RESUME,
			EVENT_DESTROY
		};

		float time;
		uint32_t thread_id;
		uint32_t scheduler_id;
		Type type;

		static SchedulerDebugEvent CreateEvent(uint32_t scheduler_id);
		static SchedulerDebugEvent PauseEvent(uint32_t scheduler_id);
		static SchedulerDebugEvent ResumeEvent(uint32_t scheduler_id);
		static SchedulerDebugEvent DestroyEvent(uint32_t scheduler_id);
	};

	struct ANVIL_DLL_EXPORT TaskDebugEvent {
		enum Type : uint8_t {
			EVENT_CREATE,
			EVENT_SCHEDULE,
			EVENT_CANCEL,
			EVENT_EXECUTE_BEGIN,
			EVENT_PAUSE,
			EVENT_RESUME,
			EVENT_EXECUTE_END,
			EVENT_DESTROY
		};

		float time;
		uint32_t thread_id;
		uint32_t task_id;
		union {
			Task* task; // EVENT_CREATE 
			struct {
				uint32_t parent_id;
				uint32_t scheduler_id;
			}; // EVENT_SCHEDULE
			bool will_yield; // EVENT_PAUSE
		};
		Type type;

		static TaskDebugEvent CreateEvent(uint32_t task_id, Task* task);
		static TaskDebugEvent ScheduleEvent(uint32_t task_id, uint32_t parent_id, uint32_t scheduler_id);
		static TaskDebugEvent CancelEvent(uint32_t task_id);
		static TaskDebugEvent ExecuteBeginEvent(uint32_t task_id);
		static TaskDebugEvent PauseEvent(uint32_t task_id, bool will_yield);
		static TaskDebugEvent ResumeEvent(uint32_t task_id);
		static TaskDebugEvent ExecuteEndEvent(uint32_t task_id);
		static TaskDebugEvent DestroyEvent(uint32_t task_id);

		typedef void(*DebugEventHandler)(SchedulerDebugEvent* scheduler_event, TaskDebugEvent* task_event);
		static void SetDebugEventHandler(DebugEventHandler handler);
	};
#endif

	/*!
		\class TaskSchedulingData
		\author Adam G. Smith
		\date December 2022
		\copyright MIT License
		\brief This structure contains the information that a Scheduler knows about a Task
	*/
	struct TaskSchedulingData {
		typedef Scheduler::Priority Priority;
		typedef Scheduler::PriorityInteger PriorityInteger;
		typedef Scheduler::PriorityValue PriorityValue;

		mutable std::shared_mutex lock;
		Task* task;
		Scheduler* scheduler;			//!< Points to the scheduler handling this task, otherwise null
#if ANVIL_USE_PARENTCHILDREN
		WeakSchedulingPtr parent;
		std::vector<WeakSchedulingPtr> children;
#endif
#if ANVIL_TASK_HAS_EXCEPTIONS
		std::exception_ptr exception;	//!< Holds an exception that is caught during execution, thrown when wait is called
#endif
#if ANVIL_DEBUG_TASKS
		uint32_t debug_id;
#endif
		PriorityValue priority;			//!< Stores the scheduling priority of the task
		struct {
			uint8_t state : 4;				//!< Stores the current state of the task
			uint8_t scheduled_flag : 1;		//!< Set to 1 when the task has been scheduled
			uint8_t wait_flag : 1;			//!< Set to 1 when it is okay to exit out of Task::Wait()
			uint8_t schedule_valid : 1;		//!< Set to 1 when when a scheduler has decided the task is valid
		};

		TaskSchedulingData();
	};

	/*!
		\class Task
		\author Adam G. Smith
		\date December 2020
		\copyright MIT License
		\brief Base structure for implementing Task based parallel programming.
		\details There are currently three optional compiler constants that can be defined (> 0) to enable extension features:
		- ANVIL_TASK_CALLBACKS : Adds user callbacks when Task is scheduled, suspended or resumed.
		- ANVIL_TASK_EXTENDED_PRIORITY : Allows the user to program finer grained control of how tasks with equal priority are handled by the scheduler.
		- ANVIL_TASK_MEMORY_OPTIMISED : Compressed the internal memory layout of the Task to from 20+ bytes to 8 bytes. Exceptions and ANVIL_TASK_EXTENDED_PRIORITY are not allowed in this mode.
		- ANVIL_TASK_DELAY_SCHEDULING : A task is not executed until Task::IsReadyToExecute() returns true.
		These features are disabled by default to avoid any overheads that would be added to scheduling systems that don't need them.
	*/
	class ANVIL_DLL_EXPORT Task {
	public:
		/*!
			\brief Describes which point in the execution cycle a Task is in.
		*/
		enum State : uint8_t {
			STATE_INITIALISED,	//!< The task has been created and is ready to be scheduled.
			STATE_SCHEDULED,	//!< The task has been scheduled and is awaiting execution.
			STATE_EXECUTING,	//!< The task is currently running.
			STATE_BLOCKED,		//!< Execution was started but the Task is currently suspended.
			STATE_COMPLETE,		//!< The execution has finished
			STATE_CANCELED		//!< The task was canceled due to user request or an exception being thrown
		};

		typedef Scheduler::Priority Priority;
		typedef Scheduler::PriorityInteger PriorityInteger;
		typedef Scheduler::PriorityValue PriorityValue;
		typedef TaskSchedulingData Data;
	private:
		Task(Task&&) = delete;
		Task(const Task&) = delete;
		Task& operator=(Task&&) = delete;
		Task& operator=(const Task&) = delete; 

#if ANVIL_TASK_FIBERS
		static void WINAPI FiberFunction(LPVOID param);
#else
		static void FiberFunction(Task& param);
#endif

		/*!
			\return Pointer to an attached scheduler, nullptr if none 
		*/
		inline Scheduler* _GetScheduler() const throw() {
			return _data->scheduler;
		}

		/*!
			\brief Calls Task::OnExecution() with proper state changes and exception handling
		*/
		void Execute() throw();

		void SetException(std::exception_ptr exception);

		StrongSchedulingPtr _data;
	protected:
		/*!
			\brief Return control to the scheduler while the task is waiting for something.
			\details Should only be called during Task::Wait or Task::OnExecution.
			If the scheduler has tasks then it will execute them, otherwise the thread will be put to sleep.
			\param condition Returns true when the task is no longer waiting for something.
			\param max_sleep_milliseconds The longest period of time the thread should sleep for before checking the wait condition again.
		*/
		inline void Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds = 33u) {
			Scheduler* scheduler = _GetScheduler();
			if (scheduler) {
				scheduler->Yield(condition, max_sleep_milliseconds);
			} else {
				throw std::runtime_error("anvil::Task::Yield : Cannot yield without a scheduler");
			}
	}

		/*!
			\brief Implements the work payload of the task.
			\details Called by the scheduler when it is ready.
			An exception thrown by this call can be retrieved when Task::Wait() is called.
		*/
		virtual void OnExecution() = 0;

#if ANVIL_TASK_CALLBACKS
		/*!
			\brief Called when the task is being added to the scheduler's work queue.
			\details If an exception is thrown then the task goes into OnScheduled and the task will not be scheduled.
			The exception can be retrieved by calling Task::Wait().
		*/
		virtual void OnScheduled() = 0;

		/*!
			\brief Called when the task is being suspended by the scheduler (because Task::Yield() was called).
			\details Exceptions thrown are handled the same way as if thrown by the wait condition function.
		*/
		virtual void OnBlock() = 0;

		/*!
			\brief Called when a suspended task is about to resume execution.
			\details Exceptions thrown are handled the same way as if thrown by Task::Execute().
		*/
		virtual void OnResume() = 0;

		/*!
			\brief Called a task is canceled.
			\see Cancel
		*/
		virtual void OnCancel() = 0;
#endif

#if ANVIL_TASK_EXTENDED_PRIORITY
		/*!
			\brief Decide which order tasks scheduled with the same priority will execute
			\return Tasks returning a higher value will execute first
		*/
		virtual PriorityInteger CalculateExtendedPriorty() const = 0;
#endif

#if ANVIL_TASK_DELAY_SCHEDULING
		virtual bool IsReadyToExecute() const throw() = 0;
#endif
	public:
		friend Scheduler;
		friend class _TaskThreadLocalData;

		/*!
			\brief Create a new task.
			\details State will be set to STATE_INITIALISED
		*/
		Task();

		/*!
			\brief Destroy the task
			\details Undefined behaviour if state is not STATE_INITIALISED, STATE_COMPLETE or STATE_CANCELED
		*/
		virtual ~Task();

		/*!
			\brief Wait for the task to complete.
			\detail If the task is not complete then Task::Yield will be called.
			If the task threw an exception during execution then it will be rethrown here.
			Subsequent calls to this function will have no effect.
		*/
		void Wait();

		/*!
			\brief Set the priority of this task.
			\details The scheduler will execute the task with the highest priority first.
			Will throw exception if the task's state is STATE_EXECUTING or STATE_BLOCKED.
		*/
		void SetPriority(Priority priority);

		/*!
			\brief Stop the task from being executed
			\details Called when an exception is thrown during scheduling or execution.
		*/
		bool Cancel() throw();

		/*!
			\return True if Task::Wait() can be called.
		*/
		inline bool IsWaitable() const throw() {
			return GetState() != STATE_INITIALISED;
		}

		/*!
			\return The current state of the Task.
		*/
		inline State GetState() const throw() {
			return static_cast<State>(_data->state);
		}

		/*!
			\return The current priority of the Task.
		*/
		inline Priority GetPriority() const throw() {
			return static_cast<Priority>(_data->priority);
		}

		/*!
			\return The parent of this task or null if there is no known parent
		*/
		inline Task* GetParent() const throw() {
#if ANVIL_USE_PARENTCHILDREN
			if (_data) {
				std::lock_guard<std::shared_mutex> lock(_data->lock);
				StrongSchedulingPtr tmp = _data->parent.lock();
				if (tmp) return tmp->task;
			}
#else
			return nullptr;
#endif
		}

		/*!
			\return The a children of this task
		*/
		inline std::vector<Task*> GetChildren() const throw() {
			std::vector<Task*> children;
#if ANVIL_USE_PARENTCHILDREN
			std::lock_guard<std::shared_mutex> lock(_data->lock);
			if (_data) {
				for (WeakSchedulingPtr& i : _data->children) {
					StrongSchedulingPtr tmp = i.lock();
					Task* t = tmp->task;
					if (t) children.push_back(t);
				}
			}
#endif
			return children;
		}

		/*!
			\param aproximate If true then count will include children that previously existed but have since been destroyed (this is faster)
			\return The number of children this task has
		*/
		inline size_t GetChildCount(bool aproximate = false) const throw() {
#if ANVIL_USE_PARENTCHILDREN
			if (aproximate) {
				if (_data) {
					std::lock_guard<std::shared_mutex> lock(_data->lock);
					return _data->children.size();
				} else {
					return 0u;
				}
			} else {
				return GetChildren().size();
			}
#else
			return 0u;
#endif
		}

		inline size_t GetRecursiveChildCount(bool aproximate = false) const throw() {
			std::vector<Task*> children = GetChildren();
			size_t count = children.size();
			for (Task* i : children) count += i->GetRecursiveChildCount(aproximate); //! \bug Small chance of task completing and being destroyed while this is executing
			return count;
		}

		/*!
			\return Return the size of the inheritance tree for this task
			\details This will be incorrect if ANVIL_TASK_PARENT or ANVIL_TASK_FAST_CHILD_COUNT are both disabled and ANVIL_TASK_FIBERS is enabled
		*/
		inline size_t GetNestingDepth() const throw() {
			size_t depth = 0u;
			Task* parent = GetParent();
			if (parent) depth += parent->GetNestingDepth() + 1u; //! \bug Small chance of task completing and being destroyed while this is executing
			return depth;
		}

		/*!
			\details Will thrown an exception if no scheduler is attached to this Task.
			\return The scheduler handling this Task.
		*/
		inline Scheduler& GetScheduler() const {
			Scheduler* const tmp = _GetScheduler();
			if (tmp == nullptr) throw std::runtime_error("Task is not attached to a scheduler");
			return *tmp;
		}

		/*!
			\brief Return the Task that is currently executing on this thread.
			\details Returns nullptr if there is no task executing on this thread.
		*/
		static Task* GetCurrentlyExecutingTask();

		/*!
			\brief Return a Task that is executing on this thread.
			\param Index the index in the execution stack, 0u is the first Task that started executing.
			\see GetNumberOfTasksExecutingOnThisThread()
		*/
		static Task* GetCurrentlyExecutingTask(size_t index);

		/*!
			\brief Return the number of Tasks that are currently executing on this thread.
		*/
		static size_t GetNumberOfTasksExecutingOnThisThread();

#if ANVIL_DEBUG_TASKS
		void PrintDebugMessage(const char* message) const;

		inline uint64_t GetDebugID() const { return _data->debug_id; }
#endif

	};

	/*!
		\class Task
		\author Adam G. Smith
		\date December 2020
		\copyright MIT License
		\brief Extends the Task structure to allow for a return values.
		\details Call TaskWithReturn::Get() instead of Task::Wait() to obtain the result.
		\see Task
	*/
	template<class R>
	class ANVIL_DLL_EXPORT TaskWithReturn : public Task {
	public:
		typedef R Result;
	private:
		Result _result;
	protected:
		inline void SetResult(Result result) {
			_result = result;
		}
	public:
		TaskWithReturn() :
			Task()
		{}

		virtual ~TaskWithReturn() {

		}

		/*!
			\brief Wait for the task to complete then return the result.
			\return The value returned by task execution.
			\see Wait
		*/
		Result& Get() {
			Wait();
			return _result;
		}

	};

}

#endif