module;

#include "SFML/Graphics.hpp"



/*
*  The core module to process our game entites.
*  Note: A lot of this layout is constrained by what MSVC is able to support in a module.
*  e.g. forward declarations can be a bit inconsistent and spit out ICEs from time to time.
*  So if the layout of things in this module looks unordered, it's probably because I had to
*  work around that.
*/

export module game_entities;

import std;
import ts_vector;
import ts_shape;
import polymorphic;


//A "thread-safe" window class which locks on access, since requests to draw may originate from multiple threads.
//not needed as at present all objects live on the main thread and all calls to draw come from there.
//
//NB: Not exported
namespace thread_safe {
	class window {
		sf::RenderWindow	m_window;
		mutable std::mutex	m_mut;

	public:

		explicit window(sf::RenderWindow&& wdw) : m_window{ std::move(wdw) } {}

		void draw(const sf::Shape& to_draw);
		void clear(sf::Color = sf::Color::Black);
		void display();
		sf::Vector2u get_size() const;
		bool is_open() const;
		void close();
		std::optional<sf::Event> poll_event();

		template<typename T>
		void draw(const thread_safe::shape<T>& shp) {
			auto _{ std::lock_guard{m_mut} };
			shp.draw(m_window);
		}

	};
}



namespace game {

	class entity;
	
	//Not 100% sold on this design, but it provides an easy way to encapsulate the core elements which ultimately other threads are going to need to be able to access
	export class data {
		thread_safe::vector<polymorphic<game::entity>>	m_entities;
		thread_safe::window								m_window;


	public:

		explicit data(sf::RenderWindow&& wdw) : m_window{ std::move(wdw) } {}

		//ENTITY MANAGEMENT-------------------------------------------

		void add_projectile(sf::Vector2f position, sf::Angle rotation);
		void add_asteroid();
		void kill_expired();

		void draw_all();
		void tick_all();

		void draw_entity(const sf::Shape& entity);
		template<typename T>
		void draw_entity(const thread_safe::shape<T>& shp) {
			m_window.draw(shp);
		}

		std::size_t num_entities() const;
		sf::Vector2u window_size() const;
		bool is_open() const;
		void close();
		std::optional<sf::Event> poll_event();
		void display();
		void clear(sf::Color = sf::Color::Black);

	};



	export class entity {

	protected:
		//Every entity in the game has a velocity, even if that velocity is 0
		sf::Vector2f		m_vel;
		//Entites may be accessed from any thread, so will need protection.
		mutable std::shared_mutex	m_mut{};
		static constexpr float tick_rate = 0.01f;

		//I would ideally prefer to require that every entity has a position by requiring one here
		//However, each entity may have a different type of drawable thing, not necessarily with a common base (which is useful, anyway)
		//And position is composited into that SFML shape.
		//So instead, we can't. Ah well.

	public:

		explicit entity(sf::Vector2f in_vec) : m_vel{ in_vec }, m_mut{} {}

		virtual ~entity() noexcept = default;

		//We need a test on whether an entity has "expired" and can safely be removed from the game
		virtual bool is_expired() const = 0;

		//For this game we can make the assumption that all entities are drawable
		//Plus we don't want to end up in hierarchy hell
		virtual void draw(game::data&) const = 0;

		//Process a single tick 
		virtual void tick(const game::data&) = 0;

		//We can still require position is queriable though
		virtual sf::Vector2f get_position() const = 0;

		//And while each shape may be different, acquiring their radii will vastly simplify things
		virtual float get_radius() const = 0;

	};


	export bool shape_within_bounds(const game::entity& ent, sf::Vector2u wdw);
	export bool shape_within_bounds(sf::Vector2f pos, float radius, sf::Vector2u wdw);



	export class projectile : public entity {
		thread_safe::shape<sf::RectangleShape>			m_shape{ sf::RectangleShape{{10, 2}}};
		std::atomic_flag								m_expired{};

		sf::Vector2f get_start_vel(sf::Angle rotation);

	public:

		explicit projectile(sf::Vector2f start_position, sf::Angle start_rotation) : entity{ get_start_vel(start_rotation) } {
			m_shape.set_position(start_position);
			m_shape.set_rotation(start_rotation);
		}

		projectile(const projectile& other) : entity{ other.m_vel }, m_shape{ other.m_shape } {
			if (other.m_expired.test()) m_expired.test_and_set();
		}
		projectile& operator=(const projectile& other);

		projectile(projectile&&) noexcept = default;
		projectile& operator=(projectile&&) noexcept  = default;

		void draw(game::data& wdw) const override;
		bool is_expired() const override;
		void tick(const game::data& wdw) override;
		sf::Vector2f get_position() const override;
		float get_radius() const override;

	};

	export class asteroid : public entity {
		thread_safe::shape<sf::CircleShape>		m_shape;
		std::uint8_t							m_size;
		std::atomic_flag						m_expired;

		static constexpr float size_scale_factor = 10;
		

	public:

		static constexpr float speed_scale_factor = 0.5f;

		explicit asteroid(sf::Vector2f initial_position, sf::Vector2f initial_velocity, std::uint8_t initial_size) 
			: entity{ initial_velocity }, m_shape{ static_cast<float>(size_scale_factor * initial_size) }, m_size{ initial_size }, m_expired{} {

			m_shape.set_position(initial_position);
		}

		asteroid(const asteroid& other) : entity{ other.m_vel }, m_shape { other.m_shape }, m_size{ other.m_size }, m_expired{} {
			if (other.m_expired.test()) {
				m_expired.test_and_set();
			}
		}
		asteroid& operator=(const asteroid& other) = default;

		asteroid(asteroid&&) noexcept = default;
		asteroid& operator=(asteroid&&) noexcept = default;

		void draw(game::data& wdw) const override;
		bool is_expired() const override;
		void tick(const game::data& wdw) override;
		sf::Vector2f get_position() const override;
		float get_radius() const override;
	};



	export class player : public entity {
		thread_safe::shape<sf::ConvexShape> m_shape;

		sf::Vector2f m_accel;

		static sf::ConvexShape create_player();
		static constexpr std::chrono::duration time_between_shots{ std::chrono::milliseconds{100} };
		static constexpr float speed_scale_factor{ 50.0f };

		enum move_state : std::uint8_t {
			forward_down	= 0b0000'0001,
			forward_up		= 0b0000'0010,
			backward_down	= 0b0000'0100,
			backward_up		= 0b0000'1000,
			left_down		= 0b0001'0000,
			left_up			= 0b0010'0000,
			right_down		= 0b0100'0000,
			right_up		= 0b1000'0000
		};

		move_state m_movement{ 0 };

	public:

		explicit player(sf::Vector2f initial_position, game::data& vec) : entity{ {0,0} }, m_shape { create_player() } {
			m_shape.set_position(initial_position);
		}

		//SFML boilerplate
		void rotate(sf::Angle angle);
		void set_position(sf::Vector2f new_position);
		void draw(game::data& in) const override;

		//Overrides
		constexpr inline bool is_expired() const override {
			//The player never expires
			return false;
		}
		void tick(const game::data&) override;
		sf::Vector2f get_position() const override;
		float get_radius() const override;

		//User functions
		void move(float quantity, const game::data& wdw);
		void rotate(float in);
		void shoot(game::data&);


	};



}