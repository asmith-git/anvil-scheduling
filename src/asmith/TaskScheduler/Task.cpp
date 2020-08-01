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

#include "asmith/TaskScheduler/Core.hpp"
#include "asmith/TaskScheduler/Scheduler.hpp"
#include "asmith/TaskScheduler/Task.hpp"

namespace asmith {
	// TaskHandle

	TaskHandle::TaskHandle(Task& task, Scheduler& scheduler, Priority priority) :
		_task(task),
		_scheduler(scheduler),
		_priority(priority)
	{}

	TaskHandle::~TaskHandle() {
		_Wait();
	}

	void TaskHandle::_Wait() {
		if(_task._state == Task::STATE_INITIALISED || _task._state==Task::STATE_COMPLETE) return;

		_scheduler.Yield([this]()->bool {
			return _task._state == Task::STATE_COMPLETE;
		});
	}

	void TaskHandle::Wait() {
		if (_task._state == Task::STATE_INITIALISED) throw std::runtime_error("Task has not been scheduled");
		_Wait();
	}

	// Task

	Task::Task() :
		_state(STATE_INITIALISED)
	{}

	Task::~Task() {

	}
}