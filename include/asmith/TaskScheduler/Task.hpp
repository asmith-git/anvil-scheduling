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

#ifndef ASMITH_SCHEDULER_TASK_HPP
#define ASMITH_SCHEDULER_TASK_HPP

#include <stdexcept>
#include "asmith/TaskScheduler/Scheduler.hpp"

namespace asmith {

	// Check for invalid exension options
#if ASMITH_TASK_MEMORY_OPTIMISED && ASMITH_TASK_EXTENDED_PRIORITY
	#error ASMITH_TASK_EXTENDED_PRIORITY is incompatible with ASMITH_TASK_MEMORY_OPTIMISED
#endif

	/*!
		\class Task
		\author Adam G. Smith
		\date December 2020
		\copyright MIT License
		\brief Base structure for implementing Task based parallel programming.
		\details There are currently three optional compiler constants that can be defined (> 0) to enable extension features:
		- ASMITH_TASK_CALLBACKS : Adds user callbacks when Task is scheduled, suspended or resumed.
		- ASMITH_TASK_EXTENDED_PRIORITY : Allows the user to program finer grained control of how tasks with equal priority are handled by the scheduler.
		- ASMITH_TASK_MEMORY_OPTIMISED : Compressed the internal memory layout of the Task to from 20+ bytes to 8 bytes. Exceptions and ASMITH_TASK_EXTENDED_PRIORITY are not allowed in this mode.
		These features are disabled by default to avoid any overheads that would be added to scheduling systems that don't need them.
	*/
	class Task {
	public:
		/*!
			\brief Describes which point in the execution cycle a Task is in.
		*/
		enum State : uint8_t {
			STATE_INITIALISED,	//!< The task has been created and is ready to be scheduled.
			STATE_SCHEDULED,	//!< The task has been scheduled and is awaiting execution.
			STATE_EXECUTING,	//!< The task is currently running.
			STATE_BLOCKED,		//!< Execution was started but the Task is currently suspended.
			STATE_COMPLETE		//!< The execution has finished or an error has occurred.
		};

#if ASMITH_TASK_EXTENDED_PRIORITY
		typedef uint32_t Priority;	
#else
		typedef uint8_t Priority;
#endif								//!< Defines the order in which Tasks are executed.

		enum : Priority {
			PRIORITY_LOWEST = 0u,										//!< The lowest prority level supported by the Scheduler.
#if ASMITH_TASK_MEMORY_OPTIMISED
			PRIORITY_HIGHEST = 64u,
#elif ASMITH_TASK_EXTENDED_PRIORITY
			PRIORITY_HIGHEST = UINT32_MAX,
#else
			PRIORITY_HIGHEST = UINT8_MAX,								
#endif	//!< The highest prority level supported by the Scheduler.
			PRIORITY_MIDDLE = PRIORITY_HIGHEST / 2u,					//!< The default priority level.
			PRIORITY_HIGH = PRIORITY_MIDDLE + (PRIORITY_MIDDLE / 2u),	//!< Halfway between PRIORITY_MIDDLE and PRIORITY_HIGHEST.
			PRIORITY_LOW = PRIORITY_MIDDLE - (PRIORITY_MIDDLE / 2u)		//!< Halfway between PRIORITY_MIDDLE and PRIORITY_LOWEST.
		};
	private:
		Task(Task&&) = delete;
		Task(const Task&) = delete;
		Task& operator=(Task&&) = delete;
		Task& operator=(const Task&) = delete;

		/*!
			\return Pointer to an attached scheduler, nullptr if none 
		*/
		Scheduler* _GetScheduler() const throw();

#if ASMITH_TASK_MEMORY_OPTIMISED
		uint8_t _scheduler_index;		//!< Remembers which scheduler this task is attached to, otherwise 0
#else
		std::exception_ptr _exception;	//!< Holds an exception that is caught during execution, thrown when wait is called
		Scheduler* _scheduler;			//!< Points to the scheduler handling this task, otherwise null
#endif
#if ASMITH_TASK_EXTENDED_PRIORITY
		float _extended_priority;		//!< Caches the last result returned by Task::GetExtendedPriority() to avoid overhead from virtual function calls, stored by Task::SetPriority()
#endif
#if ASMITH_TASK_MEMORY_OPTIMISED
		struct {
			uint8_t _priority : 4u;		//!< Stores the scheduling priority of the task
			uint8_t _state : 4u;		//!< Stores the current state of the task
		};
#else
		Priority _priority;				//!< Stores the scheduling priority of the task
		State _state;					//!< Stores the current state of the task
#endif
	protected:
		/*!
			\brief Return control to the scheduler while the task is waiting for something.
			\details Should only be called during Task::Wait or Task::Execute.
			If the scheduler has tasks then it will execute them, otherwise the thread will be put to sleep.
			\param condition Returns true when the task is no longer waiting for something.
			\param max_sleep_milliseconds The longest period of time the thread should sleep for before checking the wait condition again.
		*/
		void Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds = 33u);

		/*!
			\brief Implements the work payload of the task.
			\details Called by the scheduler when it is ready.
			An exception thrown by this call can be retrieved when Task::Wait() is called.
		*/
		virtual void Execute() = 0;

#if ASMITH_TASK_EXTENDED_PRIORITY
		/*!
			\brief Return a user defined priority extension
			\details Allows the user to have more control over the ordering of tasks with the same priority level.
			Suggested uses are to run longer tasks first, or to delay tasks that share some external resource with another task that is running.
			Called by Task::SetPriority()
		*/
		virtual float GetExtendedPriority() const;
#endif
#if ASMITH_TASK_CALLBACKS
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
#endif
	public:
		friend Scheduler;

		/*!
			\brief Create a new task.
			\details State will be set to STATE_INITIALISED
		*/
		Task();

		/*!
			\brief Destroy the task
			\details Undefined behaviour if state is not STATE_INITIALISED or STATE_COMPLETE
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
		void SetPriority(const Priority priority);

		/*!
			\return True if Task::Wait() can be called.
		*/
		inline bool IsWaitable() const throw() {
			return _state != STATE_INITIALISED;
		}

		/*!
			\return The current state of the Task.
		*/
		inline State GetState() const throw() {
			return static_cast<State>(_state);
		}

		/*!
			\return The current priority of the Task.
		*/
		inline Priority GetPriority() const throw() {
			//if (_scheduler == nullptr) throw std::runtime_error("Task is not attached to a scheduler");
			return _priority;
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
	class TaskWithReturn : public Task {
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