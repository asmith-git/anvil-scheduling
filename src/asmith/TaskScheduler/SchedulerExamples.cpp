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

#include "asmith/TaskScheduler/SchedulerExamples.hpp"

#if ANVIL_DEBUG_TASKS
#include <sstream>
#endif

namespace anvil {

	// ExampleThread

	ExampleThread::ExampleThread(ExampleScheduler& scheduler) :
		_comm_flag(COMM_EXIT),
		_scheduler(scheduler)
	{
		Start();
	}

	ExampleThread::~ExampleThread() {
		Stop();
	}

	bool ExampleThread::Start() {
		if (_comm_flag != COMM_EXIT) return false;
#if ANVIL_DEBUG_TASKS
		_scheduler.PrintDebugMessage("Scheduler %scheduler% launching new thread");
#endif
		_comm_flag = COMM_EXECUTE;
		_thread = std::thread(
			[this]()->void {
			while (true) {
				switch (_comm_flag) {
				case COMM_DISABLED:
				{
#if ANVIL_DEBUG_TASKS
					_scheduler.PrintDebugMessage("Thread %thread% put to sleep because it is disabled");
#endif
					std::unique_lock<std::mutex> lock(_scheduler._mutex);
					_scheduler._task_queue_update.wait(lock);
				}
				break;
				case COMM_EXECUTE:
#if ANVIL_DEBUG_TASKS
					_scheduler.PrintDebugMessage("Thread %thread% looking for tasks to execute");
#endif
					// Execute tasks until the flag changes
					_scheduler.Yield([this]()->bool { return _comm_flag != COMM_EXECUTE; });
					break;
				case COMM_EXIT:
#if ANVIL_DEBUG_TASKS
					_scheduler.PrintDebugMessage("Thread %thread% terminating");
#endif
					// Exit from the function (terminate thread)
					return;
				}
			}
		});

		return true;
	}

	bool ExampleThread::Stop() {
		if (!_thread.joinable()) return false;
#if ANVIL_DEBUG_TASKS
		_scheduler.PrintDebugMessage(("Scheduler %scheduler% requesting termination of thread " + (std::ostringstream() << _thread.get_id()).str()).c_str());
#endif
		_comm_flag = COMM_EXIT;
		_scheduler._task_queue_update.notify_all(); // Wake threads
		_thread.join();
		return true;
	}

	bool ExampleThread::Pause() {
		if (!_thread.joinable()) return false;
#if ANVIL_DEBUG_TASKS
		_scheduler.PrintDebugMessage(("Scheduler %scheduler% requesting pause of thread " + (std::ostringstream() << _thread.get_id()).str()).c_str());
#endif
		_comm_flag = COMM_DISABLED;
		// No reason to wake threads to tell them they need to sleep
		return true;
	}

	bool ExampleThread::Resume() {
		if (!_thread.joinable()) return false;
#if ANVIL_DEBUG_TASKS
		_scheduler.PrintDebugMessage(("Scheduler %scheduler% requesting resume of thread " + (std::ostringstream() << _thread.get_id()).str()).c_str());
#endif
		_comm_flag = COMM_EXECUTE;
		_scheduler._task_queue_update.notify_all(); // Wake threads
		return true;
	}

	// ExampleScheduler

	ExampleScheduler::ExampleScheduler() {

	}

	ExampleScheduler::~ExampleScheduler() {

	}

	// ExampleSchedulerSingleThreaded

	ExampleSchedulerSingleThreaded::ExampleSchedulerSingleThreaded() :
		_thread(*this)
	{
		_thread.Start();
	}

	ExampleSchedulerSingleThreaded::~ExampleSchedulerSingleThreaded() {
		_thread.Stop();
	}

	// ExampleSchedulerMultiThreaded

	ExampleSchedulerMultiThreaded::ExampleSchedulerMultiThreaded() :
		ExampleSchedulerMultiThreaded(4u)
	{}

	ExampleSchedulerMultiThreaded::ExampleSchedulerMultiThreaded(size_t count) {
		_thread_count = static_cast<int32_t>(count);

		for (size_t i = 0u; i < count; ++i) {
			std::shared_ptr<ExampleThread> thread(new ExampleThread(*this));
			thread->Start();
			_threads.push_back(thread);
		}
	}

	ExampleSchedulerMultiThreaded::~ExampleSchedulerMultiThreaded() {
		for (auto& thread : _threads) {
			thread->Stop();
		}
	}
}