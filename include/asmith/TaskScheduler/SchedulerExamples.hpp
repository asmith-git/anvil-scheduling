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

#ifndef ANVIL_SCHEDULER_SCHEDULER_EXAMPLE_HPP
#define ANVIL_SCHEDULER_SCHEDULER_EXAMPLE_HPP

#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include "asmith/TaskScheduler/Scheduler.hpp"

namespace anvil {

	class ExampleScheduler;

	class ANVIL_DLL_EXPORT ExampleThread {
	private:
		enum : uint32_t {
			COMM_DISABLED,
			COMM_EXECUTE,
			COMM_EXIT
		};
		std::thread _thread;
		std::atomic_uint32_t _comm_flag;
		ExampleScheduler& _scheduler;

		ExampleThread(ExampleThread&&) = delete;
		ExampleThread(const ExampleThread&) = delete;
		ExampleThread& operator=(ExampleThread&&) = delete;
		ExampleThread& operator=(const ExampleThread&) = delete;
	public:
		ExampleThread(ExampleScheduler& scheduler);
		~ExampleThread();

		bool Start();
		bool Stop();
		bool Pause();
		bool Resume();
	};

	typedef Scheduler ExampleSchedulerUnthreaded;

	class ANVIL_DLL_EXPORT ExampleScheduler : public Scheduler {
	public:
		friend ExampleThread; // Allow ExampleThread to access thread synchronisation helpers

		ExampleScheduler(size_t thread_count);
		virtual ~ExampleScheduler();
	};

	class ANVIL_DLL_EXPORT ExampleSchedulerSingleThreaded : public ExampleScheduler {
	private:
		ExampleThread _thread;
	public:
		ExampleSchedulerSingleThreaded();
		virtual ~ExampleSchedulerSingleThreaded();
	};

	class ANVIL_DLL_EXPORT ExampleSchedulerMultiThreaded : public ExampleScheduler {
	private:
		std::vector<std::shared_ptr<ExampleThread>> _threads;
	public:
		ExampleSchedulerMultiThreaded();
		ExampleSchedulerMultiThreaded(size_t count);
		virtual ~ExampleSchedulerMultiThreaded();
	};
}

#endif