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

#ifndef ASMITH_SCHEDULER_SCHEDULER_HPP
#define ASMITH_SCHEDULER_SCHEDULER_HPP

#include <functional>
#include <memory>
#include "asmith/TaskScheduler/Core.hpp"

namespace asmith {

	class TaskHandle {
	public:
		typedef uint8_t Priority;
	private:
		friend Task;
		Task& _task;
		Scheduler& _scheduler;
		Priority _priority;

		TaskHandle(Task& task, Scheduler& scheduler, Priority priority);

		void _Wait();
	public:
		~TaskHandle();
		void Wait();
	};

	class Scheduler {
	public:
		friend Task;
		typedef uint8_t Priority;

		Scheduler();
		virtual ~Scheduler();

		void Yield(const std::function<bool()>& condition);

		std::shared_ptr<TaskHandle> Schedule(Task& task, Priority priority);
	};
}

#endif