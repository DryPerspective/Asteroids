export module ts_vector;

import std;

namespace thread_safe {


	export template<typename T, typename Alloc = std::allocator<T>>
	class vector {

		std::vector<T, Alloc>		m_vec;

		mutable std::shared_mutex	m_mut{};


	public:

		vector() = default;
		vector(std::vector<T, Alloc> vec) : m_vec{ std::move(vec) } {}

		vector(const vector& other) {
			auto _{ std::lock_guard{other.m_mut} };
			m_vec = other.m_vec;
		}

		void push_back(auto&& new_elem) {
			auto _{ std::lock_guard{m_mut} };
			m_vec.push_back(std::forward<decltype(new_elem)>(new_elem));
		}

		void push_back(const T& new_elem) {
			auto _{ std::lock_guard{m_mut} };
			m_vec.push_back(new_elem);
		}

		void push_back(T&& new_elem) {
			auto _{ std::lock_guard{m_mut} };
			m_vec.push_back(std::move(new_elem));
		}

		template<typename... Args> requires std::constructible_from<T, Args...>
		void emplace_back(Args&&... args) {
			auto _{ std::lock_guard{m_mut} };
			m_vec.emplace_back(std::forward<Args>(args)...);
		}


		template<typename Func> requires std::invocable<Func, std::add_lvalue_reference_t<T>>
		void for_each(Func&& func) {
			auto _{ std::lock_guard{m_mut} };
			std::ranges::for_each(m_vec, std::forward<Func>(func));
		}

		template<typename Func> requires std::invocable<Func, std::add_lvalue_reference_t<T>>
		void erase_if(Func&& func) {
			auto _{ std::lock_guard{m_mut} };
			m_vec.erase(std::remove_if(m_vec.begin(), m_vec.end(), std::forward<Func>(func)), m_vec.end());
		}

		std::size_t size() const {
			auto _{ std::shared_lock{m_mut} };
			return m_vec.size();
		}






	};



}