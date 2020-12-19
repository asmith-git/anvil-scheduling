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

#ifndef ASMITH_SCHEDULER_CORE_HPP
#define ASMITH_SCHEDULER_CORE_HPP

#include <cstdint>

	// Check for invalid exension options
#if ASMITH_TASK_MEMORY_OPTIMISED && ASMITH_TASK_EXTENDED_PRIORITY
	#error ASMITH_TASK_EXTENDED_PRIORITY is incompatible with ASMITH_TASK_MEMORY_OPTIMISED
#endif

#if ASMITH_TASK_MEMORY_OPTIMISED
	#define ASMITH_TASK_HAS_EXCEPTIONS 0
	#define ASMITH_TASK_GLOBAL_SCHEDULER_LIST 1
#else
	#define ASMITH_TASK_HAS_EXCEPTIONS 1
	#define ASMITH_TASK_GLOBAL_SCHEDULER_LIST 0
#endif 

namespace asmith {
	class Task;
	class Scheduler;
}

#endif