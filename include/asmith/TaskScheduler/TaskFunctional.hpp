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

#ifndef ANVIL_SCHEDULER_TASK_FUNCTIONAL_HPP
#define ANVIL_SCHEDULER_TASK_FUNCTIONAL_HPP

#include "asmith/TaskScheduler/Task.hpp"

namespace anvil {

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
		*/
		template<class F>
		class ANVIL_DLL_EXPORT TaskFunctional_NoReturn final : public Task {
		public:
			typedef F Function;
		private:
			Function _payload; //!< The function object that will be called by the task
		protected:
			void OnExecution() final {
				_payload();
			}
#if ANVIL_TASK_CALLBACKS
			void OnScheduled() final {

			}
		
			void OnBlock() final {

			}

			void OnResume() final {

			}

			void OnCancel() final {

			}
#endif

#if ANVIL_TASK_EXTENDED_PRIORITY
			PriorityInteger CalculateExtendedPriorty() const final {
				return 0u;
			}
#endif

#if ANVIL_TASK_DELAY_SCHEDULING
			bool IsReadyToExecute() const throw() final {
				return true;
			}
#endif
		public:
			TaskFunctional_NoReturn() :
				Task(),
				_payload()
			{}

			TaskFunctional_NoReturn(Function payload) :
				Task(),
				_payload(std::move(payload))
			{}

			virtual ~TaskFunctional_NoReturn() {

			}

			/*!
				\brief Change the payload function object.
				\detail Will throw exception if not in Task::STATE_INITIALISED
				\param payload The new function object.
			*/
			inline void SetPayload(const Function& payload) {
				if (this->GetState() != Task::STATE_INITIALISED) throw std::runtime_error("TaskFunctional_NoReturn::SetPayload : Cannot set payload function unless Task is in STATE_INITIALISED");
				_payload = payload;
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
		*/
		template<class R, class F>
		class ANVIL_DLL_EXPORT TaskFunctional_Return final : public TaskWithReturn<R> {
		public:
			typedef F Function;
		private:
			Function _payload; //!< The function object that will be called by the task
		protected:
			void OnExecution() final {
				this->SetResult(_payload());
			}
#if ANVIL_TASK_CALLBACKS
			void OnScheduled() final {
	
			}
			
			void OnBlock() final {
	
			}

			void OnResume() final {
	
			}

			void OnCancel() final {

			}
#endif

#if ANVIL_TASK_EXTENDED_PRIORITY
			Task::PriorityInteger CalculateExtendedPriorty() const final {
				return 0u;
			}
#endif

#if ANVIL_TASK_DELAY_SCHEDULING
			bool IsReadyToExecute() const throw() final {
				return true;
			}
#endif
		public:
			TaskFunctional_Return() :
				TaskWithReturn<R>(),
				_payload()
			{}

			TaskFunctional_Return(Function payload) :
				TaskWithReturn<R>(),
				_payload(std::move(payload))
			{}
	
			virtual ~TaskFunctional_Return() {
	
			}

			/*!
				\brief Change the payload function object.
				\detail Will throw exception if not in Task::STATE_INITIALISED
				\param payload The new function object.
			*/
			inline void SetPayload(const Function& payload) {
				if (this->GetState() != Task::STATE_INITIALISED) throw std::runtime_error("TaskFunctional_NoReturn::SetPayload : Cannot set payload function unless Task is in STATE_INITIALISED");
				_payload = payload;
			}
	
		};

		/*!
			\brief Specialise this template to tell the compiler which Task child class should be used for differen function types.
			\details This was intended for allowing return values but may also have other uses.
			\tparam F The function object type
			\see TaskFunctional
			\see TaskFunctional_Return
			\see TaskFunctional_NoReturn
		*/
		template<class F, class ENABLE = void>
		struct TaskFunctionalSelector {
			typedef TaskFunctional_NoReturn<F> type;
		};

		template<class R>
		struct TaskFunctionalSelector<std::function<R()>, typename std::enable_if<!std::is_same<R, void>::value>::type> {
			typedef TaskFunctional_Return<R, std::function<R()>> type;
		};

		template<class R>
		struct TaskFunctionalSelector<R(*)(), typename std::enable_if<!std::is_same<R, void>::value>::type> {
			typedef TaskFunctional_Return<R, R(*)()> type;
		};

	}

	/*!
		\brief Alias for a Task which can wrap a function object.
		\details Uses detail::TaskFunctionalSelector to determine which class to use.
		\tparam F The function object type
		\see detail::TaskFunctionalSelector
	*/
	template<class F>
	using TaskFunctional = typename detail::TaskFunctionalSelector<F>::type;

}

#endif