module;

#include "SFML/Graphics.hpp"

export module ts_shape;

import std;

namespace thread_safe {
	//The best level of granularity is locking on access to the shape
	export template<typename Shape>
	class shape {
		Shape m_shape;
		mutable std::shared_mutex m_mut{};


	public:
		template<typename U> requires std::convertible_to<U, Shape>
		shape(U&& in_shp) : m_shape{ std::forward<U>(in_shp) } {}

		template<typename... Args> requires std::constructible_from<Shape, Args...>
		shape(Args&&... args) : m_shape{ std::forward<Args>(args)... } {}

		shape(const shape& other) {
			auto _{ std::lock_guard{other.m_mut} };
			m_shape = other.m_shape;
		
		}
		shape& operator=(const shape& other) {
			auto _{ std::scoped_lock{ m_mut, other.m_mut } };
			m_shape = other.m_shape;
			return *this;
		}

		shape(shape&&) noexcept = default;
		shape& operator=(shape&& other) noexcept {
			auto _{ std::scoped_lock{m_mut, other.m_mut} };
			m_shape = std::move(other.m_shape);
			return *this;
		}

		void set_position(sf::Vector2f new_pos) {
			auto _{ std::lock_guard{m_mut} };
			m_shape.setPosition(new_pos);
		}
		sf::Vector2f get_position() const {
			auto _{ std::shared_lock{m_mut} };
			return m_shape.getPosition();
		}

		void set_rotation(sf::Angle rot) {
			auto _{ std::lock_guard{m_mut} };
			m_shape.setRotation(rot);
		}

		sf::Angle get_rotation() const {
			auto _{ std::shared_lock{m_mut} };
			return m_shape.getRotation();
		}

		void move(sf::Vector2f move_vec) {
			auto _{ std::lock_guard{m_mut} };
			m_shape.move(move_vec);
		}

		sf::Vector2f get_origin() const {
			auto _{ std::shared_lock{m_mut} };
			return m_shape.getOrigin();
		}
		sf::Vector2f get_point(std::size_t point) const {
			auto _{ std::shared_lock{m_mut} };
			return m_shape.getPoint(point);
		}
		float get_radius() const {
			auto _{ std::shared_lock{m_mut} };
			return m_shape.getRadius();
		}
		void rotate(sf::Angle in_angle) {
			auto _{ std::lock_guard{m_mut} };
			m_shape.rotate(in_angle);
		}

		void draw(sf::RenderWindow& wdw) const {
			auto _{ std::shared_lock{m_mut} };
			wdw.draw(m_shape);
		}

		auto get_transform() const {
			auto _{ std::shared_lock{m_mut} };
			return m_shape.getTransform();
		}
	};

	export template<typename T>
		shape(T) -> shape<T>;
}