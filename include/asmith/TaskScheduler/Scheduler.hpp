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

#ifndef ANVIL_SCHEDULER_SCHEDULER_HPP
#define ANVIL_SCHEDULER_SCHEDULER_HPP

#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>
#include <memory>
#include "asmith/TaskScheduler/Core.hpp"

namespace anvil {

	class ANVIL_DLL_EXPORT Scheduler {
	public:
		struct ThreadDebugData {
			std::atomic_uint32_t tasks_executing;
			std::atomic_uint32_t sleeping;
			std::atomic_uint32_t enabled;
		};

		struct SchedulerDebugData {
			ThreadDebugData* thread_debug_data;
			std::atomic_uint32_t total_thread_count;
			std::atomic_uint32_t executing_thread_count;
			std::atomic_uint32_t sleeping_thread_count;
			std::atomic_uint32_t total_tasks_executing;
			std::atomic_uint32_t total_tasks_queued;
		};
	private:
		Scheduler(Scheduler&&) = delete;
		Scheduler(const Scheduler&) = delete;
		Scheduler& operator=(Scheduler&&) = delete;
		Scheduler& operator=(const Scheduler&) = delete;

#if ANVIL_TASK_DELAY_SCHEDULING
		std::vector<Task*> _unready_task_queue; //!< Contains tasks that have been scheduled but are not yet ready to execute
#endif
		std::vector<std::shared_ptr<Task>> _task_queue;			//!< Contains tasks that have been scheduled and are ready to execute
		void SortTaskQueue() throw();

		void RemoveNextTaskFromQueue(std::shared_ptr<Task>* tasks, uint32_t& count) throw();

		/*!
			\brief Called when a Task has been added or removed from the queue
			\details Wakes up threads that were sleeping and performs some additional scheduling logic
		*/
		void TaskQueueNotify();


#if ANVIL_TASK_DELAY_SCHEDULING
		void CheckUnreadyTasks();
#endif
	protected:
		SchedulerDebugData _scheduler_debug;
		std::condition_variable _task_queue_update;
		std::mutex _mutex;
		bool _no_execution_on_wait;

		bool TryToExecuteTask() throw();


#if ANVIL_TASK_EXTENDED_PRIORITY
		void RecalculatedExtendedPriorities();
#endif

	public:
#if ANVIL_TASK_EXTENDED_PRIORITY == 0
		enum {
			MAIN_PRIORITY_BITS = 8u,
			EXTENDED_PRIORITY_BITS = 0u
		};
		typedef uint8_t PriorityInteger;
#elif ANVIL_TASK_EXTENDED_PRIORITY <= 7u
		enum {
			MAIN_PRIORITY_BITS = 8u - ANVIL_TASK_EXTENDED_PRIORITY,
			EXTENDED_PRIORITY_BITS = ANVIL_TASK_EXTENDED_PRIORITY
		};
		typedef uint8_t PriorityInteger;
#elif ANVIL_TASK_EXTENDED_PRIORITY <= 15u
		enum {
			MAIN_PRIORITY_BITS = 16u - ANVIL_TASK_EXTENDED_PRIORITY,
			EXTENDED_PRIORITY_BITS = ANVIL_TASK_EXTENDED_PRIORITY
		};
		typedef uint16_t PriorityInteger;
#elif ANVIL_TASK_EXTENDED_PRIORITY <= 31u
		enum {
			MAIN_PRIORITY_BITS = 32u - ANVIL_TASK_EXTENDED_PRIORITY,
			EXTENDED_PRIORITY_BITS = ANVIL_TASK_EXTENDED_PRIORITY
		};
		typedef uint32_t PriorityInteger;
#else
		enum {
			MAIN_PRIORITY_BITS = 64u - ANVIL_TASK_EXTENDED_PRIORITY,
			EXTENDED_PRIORITY_BITS = ANVIL_TASK_EXTENDED_PRIORITY
		};
		typedef uint64_t PriorityInteger;
#endif

		enum Priority : PriorityInteger {
			PRIORITY_LOWEST = 0u,										//!< The lowest prority level supported by the Scheduler.
			PRIORITY_HIGHEST = (1 << MAIN_PRIORITY_BITS) - 1,			//!< The highest prority level supported by the Scheduler.
			PRIORITY_MIDDLE = PRIORITY_HIGHEST / 2u,					//!< The default priority level.
			PRIORITY_HIGH = PRIORITY_MIDDLE + (PRIORITY_MIDDLE / 2u),	//!< Halfway between PRIORITY_MIDDLE and PRIORITY_HIGHEST.
			PRIORITY_LOW = PRIORITY_MIDDLE - (PRIORITY_MIDDLE / 2u)		//!< Halfway between PRIORITY_MIDDLE and PRIORITY_LOWEST.
		};//!< Defines the order in which Tasks are executed.

	protected:

#if ANVIL_TASK_EXTENDED_PRIORITY == 0
		typedef PriorityInteger PriorityValue;
#else
		union PriorityValue {
			struct {
				PriorityInteger main : MAIN_PRIORITY_BITS;
				PriorityInteger extended : EXTENDED_PRIORITY_BITS;
			};
			PriorityInteger integer;

			PriorityValue() = default;
			~PriorityValue() = default;
			PriorityValue(Priority m, PriorityInteger e = 0u) : main(m), extended(e) {}

			inline bool operator==(const PriorityValue& other) const throw()  { return integer == other.integer; }
			inline bool operator!=(const PriorityValue& other) const throw()  { return integer != other.integer; }
			inline bool operator<(const PriorityValue& other) const throw() { return integer < other.integer; }
			inline bool operator>(const PriorityValue& other) const throw() { return integer > other.integer; }
			inline bool operator<=(const PriorityValue& other) const throw() { return integer <= other.integer; }
			inline bool operator>=(const PriorityValue& other) const throw() { return integer >= other.integer; }

			inline operator Priority() const throw() { return static_cast<Priority>(main); }
			inline PriorityValue& operator=(Priority p) throw() { main = p; return *this; }
		};

		static_assert(sizeof(PriorityValue) == sizeof(PriorityInteger), "Size of PriorityValue is different than expected");
#endif
	public:
		friend Task;

		Scheduler(size_t thread_count);
		virtual ~Scheduler();

		void RegisterAsWorkerThread();

		void Yield(const std::function<bool()>& condition, uint32_t max_sleep_milliseconds = 33u);

		void Schedule(std::shared_ptr<Task>* tasks, const uint32_t count);

		void Schedule(std::shared_ptr<Task> task, Priority priority);

		inline void Schedule(std::shared_ptr<Task> task) {
			Schedule(&task, 1u);
		}

		template<class T>
		void Schedule(std::shared_ptr<T>* tasks, uint32_t count) {
			// Allocate a small buffer in stack memory
			enum { TASK_BLOCK = 256 };
			std::shared_ptr<Task> tasks2[TASK_BLOCK];

			// While there are tasks left to schedule
			while (count > 0u) {
				// Add tasks to the buffer
				uint32_t count2 = count;
				if (count2 > TASK_BLOCK) count2 = TASK_BLOCK;
				for (uint32_t i = 0u; i < count2; ++i) tasks2[i] = tasks[i];
				count -= count2;
				tasks += count2;

				// Schedule the tasks
				Schedule(tasks2, count2);
			}
		}

		template<class T>
		inline void Schedule(const std::vector<T>& tasks) {
			Schedule(tasks.data(), static_cast<uint32_t>(tasks.size()));
		}

		inline void SetExecutionOnTaskWait(bool execute) {
			_no_execution_on_wait = execute;
		}

#if ANVIL_DEBUG_TASKS
		void PrintDebugMessage(const char* message) const;
#endif

		uint32_t GetThisThreadIndex() const;
		ThreadDebugData* GetDebugDataForThread(const uint32_t index);
		inline ThreadDebugData* GetDebugDataForThisThread() { return GetDebugDataForThread(GetThisThreadIndex()); }
		SchedulerDebugData& GetDebugData();


		/*!
			\brief Return the total number of threads.
		*/
		inline size_t GetThreadCount() const throw() { return const_cast<Scheduler*>(this)->GetDebugData().total_thread_count; }

		/*!
			\brief Return the number of threads that are currently executing tasks.
		*/
		inline size_t GetExecutingThreadCount() const throw() { return const_cast<Scheduler*>(this)->GetDebugData().executing_thread_count; }

		/*!
			\brief Return the number of threads that are currently not executing tasks.
		*/
		inline size_t GetSleepingThreadCount() const throw() { return const_cast<Scheduler*>(this)->GetDebugData().sleeping_thread_count; }
	};
}

#endif