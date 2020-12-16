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

	namespace detail {

		/*!
			\class TaskFunctional_NoReturn
			\author Adam G. Smith
			\date December 2020
			\copyright MIT License
			\brief Wrapper for a function object without return value.
			\details Could be a raw function pointer or a std::function.
			\tparam F The function object type that this Task will wrap.
			\see TaskFunctional
			\see TaskFunctional_Return
		*/
		template<class F>
		class TaskFunctional_NoReturn final : public Task {
		public:
			typedef F Function;
		private:
			Function _payload; //!< The function object that will be called by the task
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
			TaskFunctional_NoReturn(Function&& payload) :
				Task(),
				_payload(std::move(payload))
			{}

			TaskFunctional_NoReturn(const Function& payload) :
				Task(),
				_payload(payload)
			{}

			virtual ~TaskFunctional_NoReturn() {

			}

		};

		/*!
			\class TaskFunctional_Return
			\author Adam G. Smith
			\date December 2020
			\copyright MIT License
			\brief Wrapper for a function object with return value.
			\details Could be a raw function pointer or a std::function.
			\tparam F The function object type that this Task will wrap.
			\see TaskFunctional
			\see TaskFunctional_NoReturn
		*/
		template<class R, class F>
		class TaskFunctional_Return final : public TaskWithReturn<R> {
		public:
			typedef F Function;
		private:
			Function _payload; //!< The function object that will be called by the task
		protected:
			void Execute() final {
				_result = _payload();
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
			TaskFunctional_Return(Function&& payload) :
				TaskWithReturn<R>(),
				_payload(std::move(payload))
			{}
	
			TaskFunctional_Return(const Function& payload) :
				TaskWithReturn<R>(),
				_payload(payload)
			{}
	
			virtual ~TaskFunctional_Return() {
	
			}
	
		};

		template<class F>
		struct TaskFunctionalSelector {
			typedef TaskFunctional_NoReturn<F> type;
		};

		template<class R>
		struct TaskFunctionalSelector<std::function<R()>> {
			typedef TaskFunctional_Return<R, std::function<R()>> type;
		};

		template<class R>
		struct TaskFunctionalSelector<R(*)()> {
			typedef TaskFunctional_Return<R, R(*)()> type;
		};

	}

	template<class F>
	using TaskFunctional = typename detail::TaskFunctionalSelector<F>::type;

}

#endif