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

#ifndef ANVIL_SCHEDULER_CORE_HPP
#define ANVIL_SCHEDULER_CORE_HPP

#include <cstdint>

	// Check for invalid exension options
#if ANVIL_TASK_MEMORY_OPTIMISED && ANVIL_TASK_EXTENDED_PRIORITY
	#error ANVIL_TASK_EXTENDED_PRIORITY is incompatible with ANVIL_TASK_MEMORY_OPTIMISED
#endif

#if ANVIL_TASK_MEMORY_OPTIMISED
	#define ANVIL_TASK_HAS_EXCEPTIONS 0
	#define ANVIL_TASK_GLOBAL_SCHEDULER_LIST 1
#else
	#define ANVIL_TASK_HAS_EXCEPTIONS 1
	#define ANVIL_TASK_GLOBAL_SCHEDULER_LIST 0
#endif 

#ifndef ANVIL_NO_EXECUTE_ON_WAIT
	#define ANVIL_NO_EXECUTE_ON_WAIT 0
#endif

namespace anvil {
	class Task;
	class Scheduler;
}

#endif