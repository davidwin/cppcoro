///////////////////////////////////////////////////////////////////////////////
// Copyright (c) Lewis Baker
// Licenced under MIT license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////
#ifndef CPPCORO_RECURSIVE_GENERATOR_HPP_INCLUDED
#define CPPCORO_RECURSIVE_GENERATOR_HPP_INCLUDED

#include <cppcoro/generator.hpp>

#include <experimental/coroutine>
#include <type_traits>
#include <utility>
#include <cassert>
#include <functional>

namespace cppcoro
{
	template<typename T, bool NoExcept = false>
	class recursive_generator
	{
	public:

		class promise_type final : public detail::exception_promise<NoExcept>
		{
		public:

			promise_type() noexcept
				: m_value(nullptr)
				, m_root(this)
				, m_parentOrLeaf(this)
			{}

			auto get_return_object() noexcept
			{
				return recursive_generator<T,NoExcept>{ *this };
			}

			std::experimental::suspend_always initial_suspend() noexcept
			{
				return {};
			}

			std::experimental::suspend_always final_suspend() noexcept
			{
				return {};
			}

			void return_void() noexcept {}

			std::experimental::suspend_always yield_value(T& value) noexcept
			{
				m_value = std::addressof(value);
				return {};
			}

			std::experimental::suspend_always yield_value(T&& value) noexcept
			{
				m_value = std::addressof(value);
				return {};
			}

			auto yield_value(recursive_generator&& generator) noexcept
			{
				return yield_value(generator);
			}

			auto yield_value(recursive_generator& generator) noexcept
			{
				struct awaitable
				{

					awaitable(promise_type* childPromise)
						: m_childPromise(childPromise)
					{}

					bool await_ready() noexcept
					{
						return this->m_childPromise == nullptr;
					}

					void await_suspend(std::experimental::coroutine_handle<promise_type>) noexcept
					{}

					void await_resume() noexcept( NoExcept )
					{
						if (this->m_childPromise != nullptr)
						{
							this->m_childPromise->rethrow_if_exception();
						}
					}

				private:
					promise_type* m_childPromise;
				};

				if (generator.m_promise != nullptr)
				{
					m_root->m_parentOrLeaf = generator.m_promise;
					generator.m_promise->m_root = m_root;
					generator.m_promise->m_parentOrLeaf = this;
					generator.m_promise->resume();

					if (!generator.m_promise->is_complete())
					{
						return awaitable{ generator.m_promise };
					}

					m_root->m_parentOrLeaf = this;
				}

				return awaitable{ nullptr };
			}

			// Don't allow any use of 'co_await' inside the recursive_generator coroutine.
			template<typename U>
			std::experimental::suspend_never await_transform(U&& value) = delete;

			void destroy() noexcept
			{
				std::experimental::coroutine_handle<promise_type>::from_promise(*this).destroy();
			}

			bool is_complete() noexcept
			{
				return std::experimental::coroutine_handle<promise_type>::from_promise(*this).done();
			}

			T& value() noexcept
			{
				assert(this == m_root);
				assert(!is_complete());
				return *(m_parentOrLeaf->m_value);
			}

			void pull() noexcept
			{
				assert(this == m_root);
				assert(!m_parentOrLeaf->is_complete());

				m_parentOrLeaf->resume();

				while (m_parentOrLeaf != this && m_parentOrLeaf->is_complete())
				{
					m_parentOrLeaf = m_parentOrLeaf->m_parentOrLeaf;
					m_parentOrLeaf->resume();
				}
			}

		private:

			void resume() noexcept
			{
				std::experimental::coroutine_handle<promise_type>::from_promise(*this).resume();
			}

			std::add_pointer_t<T> m_value;

			promise_type* m_root;

			// If this is the promise of the root generator then this field
			// is a pointer to the leaf promise.
			// For non-root generators this is a pointer to the parent promise.
			promise_type* m_parentOrLeaf;

		};

		recursive_generator() noexcept
			: m_promise(nullptr)
		{}

		recursive_generator(promise_type& promise) noexcept
			: m_promise(&promise)
		{}

		recursive_generator(recursive_generator&& other) noexcept
			: m_promise(other.m_promise)
		{
			other.m_promise = nullptr;
		}

		recursive_generator(const recursive_generator& other) = delete;
		recursive_generator& operator=(const recursive_generator& other) = delete;

		~recursive_generator()
		{
			if (m_promise != nullptr)
			{
				m_promise->destroy();
			}
		}

		recursive_generator& operator=(recursive_generator&& other) noexcept
		{
			if (this != &other)
			{
				if (m_promise != nullptr)
				{
					m_promise->destroy();
				}

				m_promise = other.m_promise;
				other.m_promise = nullptr;
			}

			return *this;
		}

		class iterator
		{
		public:

			using iterator_category = std::input_iterator_tag;
			// What type should we use for counting elements of a potentially infinite sequence?
			using difference_type = std::ptrdiff_t;
			using value_type = std::remove_reference_t<T>;
			using reference = std::conditional_t<std::is_reference_v<T>, T, T&>;
			using pointer = std::add_pointer_t<T>;

			iterator() noexcept
				: m_promise(nullptr)
			{}

			explicit iterator(promise_type* promise) noexcept
				: m_promise(promise)
			{}

			bool operator==(const iterator& other) const noexcept
			{
				return m_promise == other.m_promise;
			}

			bool operator!=(const iterator& other) const noexcept
			{
				return m_promise != other.m_promise;
			}

			iterator& operator++() noexcept(NoExcept)
			{
				assert(m_promise != nullptr);
				assert(!m_promise->is_complete());

				m_promise->pull();
				if (m_promise->is_complete())
				{
					auto* temp = m_promise;
					m_promise = nullptr;
					temp->rethrow_if_exception();
				}

				return *this;
			}

			void operator++(int) noexcept(NoExcept)
			{
				(void)operator++();
			}

			reference operator*() const noexcept
			{
				assert(m_promise != nullptr);
				return static_cast<reference>(m_promise->value());
			}

			pointer operator->() const noexcept
			{
				return std::addressof(operator*());
			}

		private:

			promise_type* m_promise;

		};

		iterator begin() noexcept(NoExcept)
		{
			if (m_promise != nullptr)
			{
				m_promise->pull();
				if (!m_promise->is_complete())
				{
					return iterator(m_promise);
				}

				m_promise->rethrow_if_exception();
			}

			return iterator(nullptr);
		}

		iterator end() noexcept
		{
			return iterator(nullptr);
		}

		void swap(recursive_generator& other) noexcept
		{
			std::swap(m_promise, other.m_promise);
		}

	private:

		friend class promise_type;

		promise_type* m_promise;

	};

	template<typename T, bool NoExcept>
	void swap(recursive_generator<T,NoExcept>& a, recursive_generator<T,NoExcept>& b) noexcept
	{
		a.swap(b);
	}

	// Note: When applying fmap operator to a recursive_generator we just yield a non-recursive
	// generator since we generally won't be using the result in a recursive context.
	template<typename FUNC, typename T, bool NoExcept>
	generator<std::invoke_result_t<FUNC&, typename recursive_generator<T,NoExcept>::iterator::reference>> fmap(FUNC func, recursive_generator<T,NoExcept> source)
	{
		for (auto&& value : source)
		{
			co_yield std::invoke(func, static_cast<decltype(value)>(value));
		}
	}
}

#endif
