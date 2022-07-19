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

#include "asmith/TaskScheduler/SchedulerExamples.hpp"

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
		_comm_flag = COMM_EXECUTE;
		_thread = std::thread([this]()->void {

			_scheduler.RegisterAsWorkerThread();

			while (true) {
				switch (_comm_flag) {
				case COMM_DISABLED:
				{
					std::unique_lock<std::mutex> lock(_scheduler._condition_mutex);
					_scheduler._task_queue_update.wait(lock);
				}
				break;
				case COMM_EXECUTE:
					// Execute tasks until the flag changes
					_scheduler.Yield([this]()->bool { return _comm_flag != COMM_EXECUTE; }, 1000u);
					break;
				case COMM_EXIT:
					// Exit from the function (terminate thread)
					return;
				}
			}
		});

		return true;
	}

	bool ExampleThread::Stop() {
		if (!_thread.joinable()) return false;
		_comm_flag = COMM_EXIT;
		_scheduler._task_queue_update.notify_all(); // Wake threads
		_thread.join();
		return true;
	}

	bool ExampleThread::Pause() {
		if (!_thread.joinable()) return false;
		_comm_flag = COMM_DISABLED;
		// No reason to wake threads to tell them they need to sleep
		return true;
	}

	bool ExampleThread::Resume() {
		if (!_thread.joinable()) return false;
		_comm_flag = COMM_EXECUTE;
		_scheduler._task_queue_update.notify_all(); // Wake threads
		return true;
	}

	// ExampleScheduler

	ExampleScheduler::ExampleScheduler(size_t thread_count) :
		Scheduler(thread_count)
	{}

	ExampleScheduler::~ExampleScheduler() {

	}

	// ExampleSchedulerSingleThreaded

	ExampleSchedulerSingleThreaded::ExampleSchedulerSingleThreaded() :
		ExampleScheduler(0u),
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

	ExampleSchedulerMultiThreaded::ExampleSchedulerMultiThreaded(size_t thread_count) :
		ExampleScheduler(thread_count)
	{
		for (size_t i = 0u; i < thread_count; ++i) {
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