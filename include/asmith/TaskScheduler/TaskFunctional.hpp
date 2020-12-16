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

#ifndef ASMITH_SCHEDULER_TASK_FUNCTIONAL_HPP
#define ASMITH_SCHEDULER_TASK_FUNCTIONAL_HPP

#include "asmith/TaskScheduler/Task.hpp"

namespace asmith {

	/*!
		\class Task
		\author Adam G. Smith
		\date December 2020
		\copyright MIT License
		\brief Wrapper for a function object.
		\details Could be a raw function pointer or a std::function.
		\tparam F The function object type that this Task will wrap.
	*/
	template<class F>
	class TaskFunctional final : public Task {
	private:
		F _payload; //!< The function object that will be called by the task
	protected:
		void Execute() final {
			_payload();
		}
#if ASMITH_TASK_CALLBACKS
		void OnScheduled() final {

		}
		
		void OnBlock() final {

	}
		void OnResume() final {

		}
#endif
	public:
		TaskFunctional(F&& payload) :
			Task(),
			_payload(std::move(payload))
		{}

		TaskFunctional(const F& payload) :
			Task(),
			_payload(payload)
		{}

		virtual ~TaskFunctional() {

		}

	};

}

#endif