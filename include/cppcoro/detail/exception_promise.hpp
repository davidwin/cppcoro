///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////
#ifndef CPPCORO_EXCEPTION_PROMISE_HPP_INCLUDED
#define CPPCORO_EXCEPTION_PROMISE_HPP_INCLUDED

#include <exception>

namespace cppcoro
{
	namespace detail
	{
		class empty_base
		{
		};

		template<bool NoExcept, typename Base = empty_base>
		class exception_promise;

		template<typename Base>
		class exception_promise<false, Base> : public Base
		{
		protected:
			exception_promise() noexcept = default;
			exception_promise(const exception_promise&) = delete;
			exception_promise(exception_promise&&) = delete;

			std::exception_ptr m_exception = nullptr;

		public:
			void unhandled_exception() noexcept { m_exception = std::current_exception(); }

			void rethrow_if_exception()
			{
				if (m_exception != nullptr)
				{
					std::rethrow_exception(std::move(m_exception));
				}
			}
		};

		template<typename Base>
		class exception_promise<true, Base> : public Base
		{
		protected:
			exception_promise() noexcept = default;
			exception_promise(const exception_promise&) = delete;
			exception_promise(exception_promise&&) = delete;

		public:
			void unhandled_exception() noexcept { std::terminate(); }

			void rethrow_if_exception() noexcept {}
		};
	}
}

#endif
