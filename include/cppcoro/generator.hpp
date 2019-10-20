///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////
#ifndef CPPCORO_GENERATOR_HPP_INCLUDED
#define CPPCORO_GENERATOR_HPP_INCLUDED

#include <cppcoro/detail/exception_promise.hpp>
#include <experimental/coroutine>
#include <type_traits>
#include <utility>
#include <exception>
#include <iterator>

namespace cppcoro
{
	template<typename T, bool NoExcept = false>
	class generator;

	namespace detail
	{
		template<typename T, bool NoExcept>
		class generator_promise final : public exception_promise<NoExcept>
		{
		public:

			using value_type = std::remove_reference_t<T>;
			using reference_type = std::conditional_t<std::is_reference_v<T>, T, T&>;
			using pointer_type = value_type*;

			generator_promise() = default;

			generator<T,NoExcept> get_return_object() noexcept;

			constexpr std::experimental::suspend_always initial_suspend() const { return {}; }
			constexpr std::experimental::suspend_always final_suspend() const { return {}; }

			template<
				typename U = T,
				std::enable_if_t<!std::is_rvalue_reference<U>::value, int> = 0>
			std::experimental::suspend_always yield_value(std::remove_reference_t<T>& value) noexcept
			{
				m_value = std::addressof(value);
				return {};
			}

			std::experimental::suspend_always yield_value(std::remove_reference_t<T>&& value) noexcept
			{
				m_value = std::addressof(value);
				return {};
			}

			void return_void() const noexcept
			{
			}

			reference_type value() const noexcept
			{
				return static_cast<reference_type>(*m_value);
			}

			// Don't allow any use of 'co_await' inside the generator coroutine.
			template<typename U>
			std::experimental::suspend_never await_transform(U&& value) = delete;

		private:

			pointer_type m_value;

		};

		struct generator_sentinel {};

		template<typename T, bool NoExcept>
		class generator_iterator
		{
			using coroutine_handle = std::experimental::coroutine_handle<generator_promise<T,NoExcept>>;

		public:

			using iterator_category = std::input_iterator_tag;
			// What type should we use for counting elements of a potentially infinite sequence?
			using difference_type = std::ptrdiff_t;
			using value_type = typename generator_promise<T,NoExcept>::value_type;
			using reference = typename generator_promise<T,NoExcept>::reference_type;
			using pointer = typename generator_promise<T,NoExcept>::pointer_type;
            static constexpr bool yield_noexcept = NoExcept;

			// Iterator needs to be default-constructible to satisfy the Range concept.
			generator_iterator() noexcept
				: m_coroutine(nullptr)
			{}

			explicit generator_iterator(coroutine_handle coroutine) noexcept
				: m_coroutine(coroutine)
			{}

			friend bool operator==(const generator_iterator& it, generator_sentinel) noexcept
			{
				return !it.m_coroutine || it.m_coroutine.done();
			}

			friend bool operator!=(const generator_iterator& it, generator_sentinel s) noexcept
			{
				return !(it == s);
			}

			friend bool operator==(generator_sentinel s, const generator_iterator& it) noexcept
			{
				return (it == s);
			}

			friend bool operator!=(generator_sentinel s, const generator_iterator& it) noexcept
			{
				return it != s;
			}

			generator_iterator& operator++() noexcept(NoExcept)
			{
				m_coroutine.resume();
				if (m_coroutine.done())
				{
					m_coroutine.promise().rethrow_if_exception();
				}

				return *this;
			}

			// Need to provide post-increment operator to implement the 'Range' concept.
			void operator++(int) noexcept(NoExcept)
			{
				(void)operator++();
			}

			reference operator*() const noexcept
			{
				return m_coroutine.promise().value();
			}

			pointer operator->() const noexcept
			{
				return std::addressof(operator*());
			}

		private:

			coroutine_handle m_coroutine;
		};
	}

	template<typename T, bool NoExcept>
	class [[nodiscard]] generator
	{
	public:

		using promise_type = detail::generator_promise<T,NoExcept>;
		using iterator = detail::generator_iterator<T,NoExcept>;

		generator() noexcept
			: m_coroutine(nullptr)
		{}

		generator(generator&& other) noexcept
			: m_coroutine(other.m_coroutine)
		{
			other.m_coroutine = nullptr;
		}

		generator(const generator& other) = delete;

		~generator()
		{
			if (m_coroutine)
			{
				m_coroutine.destroy();
			}
		}

		generator& operator=(generator other) noexcept
		{
			swap(other);
			return *this;
		}

		iterator begin() noexcept(NoExcept)
		{
			if (m_coroutine)
			{
				m_coroutine.resume();
				if (m_coroutine.done())
				{
					m_coroutine.promise().rethrow_if_exception();
				}
			}

			return iterator{ m_coroutine };
		}

		detail::generator_sentinel end() noexcept
		{
			return detail::generator_sentinel{};
		}

		void swap(generator& other) noexcept
		{
			std::swap(m_coroutine, other.m_coroutine);
		}

	private:

		friend class detail::generator_promise<T, NoExcept>;

		explicit generator(std::experimental::coroutine_handle<promise_type> coroutine) noexcept
			: m_coroutine(coroutine)
		{}

		std::experimental::coroutine_handle<promise_type> m_coroutine;

	};

	template<typename T, bool NoExcept>
	void swap(generator<T,NoExcept>& a, generator<T,NoExcept>& b)
	{
		a.swap(b);
	}

	namespace detail
	{
		template<typename T, bool NoExcept>
		generator<T,NoExcept> generator_promise<T,NoExcept>::get_return_object() noexcept
		{
			using coroutine_handle = std::experimental::coroutine_handle<generator_promise<T,NoExcept>>;
			return generator<T,NoExcept> { coroutine_handle::from_promise(*this) };
		}
	}

	template<typename FUNC, typename T, bool NoExcept>
	generator<std::invoke_result_t<FUNC&, typename generator<T,NoExcept>::iterator::reference>> fmap(FUNC func, generator<T,NoExcept> source)
	{
		for (auto&& value : source)
		{
			co_yield std::invoke(func, static_cast<decltype(value)>(value));
		}
	}
}

#endif
