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

#ifndef ANVIL_SCHEDULER_CORE_HPP
#define ANVIL_SCHEDULER_CORE_HPP
#include <cstdint>

// Include the options header if it exists
#if __has_include("UserSchedulerOptions.hpp")
	#include "UserSchedulerOptions.hpp"

// Otherwise define default options
#else
	#define ANVIL_TASK_EXTENDED_PRIORITY 0
	#define ANVIL_TASK_HAS_EXCEPTIONS 1
	#define ANVIL_NO_EXECUTE_ON_WAIT 0
	#define ANVIL_TASK_CALLBACKS 0
	#define ANVIL_DEBUG_TASKS 0
	#define ANVIL_TASK_PARENT 1
	#define ANVIL_TASK_FAST_CHILD_COUNT 1
	#define ANVIL_TASK_DELAY_SCHEDULING 0
	#define ANVIL_TASK_FIBERS 0
	#define  ANVIL_DLL_EXPORT __declspec(dllimport)
#endif

// Define options that are missing

#ifndef ANVIL_TASK_EXTENDED_PRIORITY
	#define ANVIL_TASK_EXTENDED_PRIORITY 0
#endif

#ifndef ANVIL_TASK_HAS_EXCEPTIONS
	#define ANVIL_TASK_HAS_EXCEPTIONS 0
#endif

#ifndef ANVIL_NO_EXECUTE_ON_WAIT
	#define ANVIL_NO_EXECUTE_ON_WAIT 0
#endif

#ifndef ANVIL_TASK_CALLBACKS
	#define ANVIL_TASK_CALLBACKS 0
#endif

#ifndef ANVIL_DEBUG_TASKS
	#define ANVIL_DEBUG_TASKS 0
#endif

#ifndef ANVIL_TASK_PARENT
	#define ANVIL_TASK_PARENT 1
#endif

#ifndef ANVIL_TASK_FAST_CHILD_COUNT
	#define ANVIL_TASK_FAST_CHILD_COUNT 0
#endif

#if  ANVIL_TASK_PARENT
	#undef ANVIL_TASK_FAST_CHILD_COUNT
	#define ANVIL_TASK_FAST_CHILD_COUNT 0
#endif

#ifndef ANVIL_TASK_DELAY_SCHEDULING
	#define ANVIL_TASK_DELAY_SCHEDULING 0
#endif

#ifndef ANVIL_DLL_EXPORT
	#define  ANVIL_DLL_EXPORT __declspec(dllimport)
#endif

// Protection from conflicting definitions in common headers

#ifdef Yield // Windows.h
	#undef Yield
#endif

// Check for invalid exension options

// Early definition of the main classes in this library

namespace anvil {
	class Task;
	struct TaskSchedulingData;
	class Scheduler;
}

#endif