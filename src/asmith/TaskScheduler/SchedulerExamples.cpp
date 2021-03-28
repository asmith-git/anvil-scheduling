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

namespace anvil {

	// ExampleThread

	ExampleThread::ExampleThread(Scheduler& scheduler) :
		_comm_flag(COMM_DISABLED),
		_scheduler(scheduler)
	{}

	ExampleThread::~ExampleThread() {
		Stop();
	}

	bool ExampleThread::Start() {
		if (_comm_flag != COMM_DISABLED) return false;
		_thread = std::thread(
			[this]()->void {
				while (true) {
					switch (_comm_flag) {
					case COMM_DISABLED:
						// Sleep for 33 milliseconds then check again
						std::this_thread::sleep_for(std::chrono::milliseconds(33));
						break;
					case COMM_EXECUTE:
						// Execute tasks until the flag changes
						_scheduler.Yield([this]()->bool { return _comm_flag != COMM_EXECUTE; });
						break;
					case COMM_EXIT:
						// Exit from the function (terminate thread)
						return;
					}
				}
			}
		);
		_comm_flag = COMM_EXECUTE;
		return true;
	}

	bool ExampleThread::Stop() {
		if (!_thread.joinable()) return false;
		_comm_flag = COMM_EXIT;
		_thread.join();
		return true;
	}

	bool ExampleThread::Pause() {
		if (!_thread.joinable()) return false;
		_comm_flag = COMM_DISABLED;
		return true;
	}

	bool ExampleThread::Resume() {
		if (!_thread.joinable()) return false;
		_comm_flag = COMM_EXECUTE;
		return true;
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