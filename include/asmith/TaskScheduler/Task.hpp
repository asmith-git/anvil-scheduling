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

	class Task {
	public:
		enum State : uint8_t {
			STATE_INITIALISED,
			STATE_SCHEDULED,
			STATE_EXECUTING,
			STATE_BLOCKED,
			STATE_COMPLETE
		};

		typedef uint8_t Priority;

		enum : Priority {
			PRIORITY_LOWEST = 0u,
			PRIORITY_HIGHEST = 255u,
			PRIORITY_MIDDLE = PRIORITY_HIGHEST / 2u,
			PRIORITY_HIGH = PRIORITY_MIDDLE + (PRIORITY_MIDDLE / 2u),
			PRIORITY_LOW = PRIORITY_MIDDLE - (PRIORITY_MIDDLE / 2u)
		};
	private:
		std::shared_ptr<TaskHandle> _handle;
		State _state;
	protected:
		void Yield(const std::function<bool()>& condition);
		virtual void Execute() = 0;

#if ASMITH_TASK_CALLBACKS
		virtual void OnScheduled() = 0;
		virtual void OnBlock() = 0;
		virtual void OnResume() = 0;
#endif
	public:
		friend Scheduler;
		friend TaskHandle;

		Task();
		virtual ~Task();

		inline State GetState() const throw() {
			return _state;
		}

		inline Priority GetPriority() const throw() {
			TaskHandle* const handle = _handle.get();
			if (handle == nullptr) throw std::runtime_error("Task is not attached to a scheduler");
			return handle->_priority;
		}

		inline Scheduler& GetScheduler() const {
			TaskHandle* const handle = _handle.get();
			if (handle == nullptr) throw std::runtime_error("Task is not attached to a scheduler");
			return handle->_scheduler;
		}
	};
}

#endif