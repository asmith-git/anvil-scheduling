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

#ifndef ASMITH_SCHEDULER_MULTITASK_HPP
#define ASMITH_SCHEDULER_MULTITASK_HPP

#include "asmith/TaskScheduler/Task.hpp"
#include "asmith/TaskScheduler/Scheduler.hpp"

namespace asmith {

	class MultiTask {
	private:
		class SubTask final : public Task {
		private:
			MultiTask& _parent;
			const uint32_t _index;
		protected:
			void Execute() final;
#if ASMITH_TASK_CALLBACKS
			void OnScheduled() final;
			void OnBlock() final;
			void OnResume() final;
#endif
		public:
			SubTask(MultiTask& parent, const uint32_t index);
			virtual ~SubTask();
		};

		class Handle final : public TaskHandle {
		private:
			MultiTask::SubTask* _subtasks;
			detail::UniqueTaskHandle* _handles;
			const uint32_t _count;

			void _Wait();
		public:
			friend Scheduler;

			Handle(MultiTask& parent, const uint32_t count, Scheduler& scheduler, Task::Priority priority);
			~Handle();
			void Wait() final;
		};

		uint32_t _count;
	protected:
		virtual void Execute(const uint32_t index) = 0;

#if ASMITH_TASK_CALLBACKS
		virtual void OnScheduled(const uint32_t index) = 0;
		virtual void OnBlock(const uint32_t index) = 0;
		virtual void OnResume(const uint32_t index) = 0;
#endif
		inline uint32_t GetSubtaskCount() const throw() {
			return _count;
		}
	public:
		friend Scheduler;

		MultiTask();
		virtual ~MultiTask();
	};
}

#endif