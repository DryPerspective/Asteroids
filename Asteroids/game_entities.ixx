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
import ts_queue;
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
	class asteroid;
	
	//Not 100% sold on this design, but it provides an easy way to encapsulate the core elements which ultimately other threads are going to need to be able to access
	export class data {
		//You may ask - at this current moment in time we are only storing projectiles here, so why not a vector of just projectiles?
		//The simple answer is futureproofing.
		//If we split to just asteroids and projectiles, then wanted to add a new entity later (e.g. floating score text)
		//then we'd need yet another vector.
		//Asteroids are the special case, projectiles are not. So they should go in the general storage.
		thread_safe::vector<polymorphic<game::entity>>	m_entities;
		thread_safe::window								m_window;

		//The only collisions which exist are with asteroids
		//So we keep a separate tally of the asteroids in the simulation. This gives us two benefits
		//The first is that we don't check collisions between things which are never going to collide anyway
		//The second is it allows us to easily avoid locks when scanning down the list of entities from within the list of entities.
		thread_safe::vector<std::unique_ptr<asteroid>> m_asteroids;
		thread_safe::queue<std::unique_ptr<asteroid>> m_incoming_asteroids;
		


	public:

		explicit data(sf::RenderWindow&& wdw) : m_window{ std::move(wdw) } {}

		//ENTITY MANAGEMENT-------------------------------------------

		void add_projectile(sf::Vector2f position, sf::Angle rotation);
		void add_asteroid();
		void add_asteroid(asteroid&& in);
		void kill_expired();

		void draw_all();
		void tick_entities();

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

		//We need to special case the asteroids a little
		//to track collisions
		template<typename Func>
		void for_all_asteroids(Func&& func) {
			m_asteroids.for_each(std::forward<Func>(func));
		}

		void tick();

	};



	export class entity {

	protected:
		//Every entity in the game has a velocity, even if that velocity is 0
		sf::Vector2f		m_vel;
		//Entites may be accessed from any thread, so will need protection.
		mutable std::shared_mutex	m_mut{};
		static constexpr float tick_rate = 0.01f;
		//The max travel speed of any game object. Useful to ensure invariants and prevent things
		//scaling upwards infinitely.
		static constexpr float max_speed{ 3 / tick_rate };

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
		virtual void tick(game::data&) = 0;

		//We can still require position is queriable though
		virtual sf::Vector2f get_position() const = 0;

		//And while each shape may be different, acquiring their radii will vastly simplify things
		virtual float get_radius() const = 0;

		//Whether we have collided with another entity
		virtual bool is_collided(const entity& other) const {
			//The most basic detection is to model our two objects as two circles
			//This may be refined on a per-class basis
			auto distance_between_centers{ (get_position() - other.get_position()).length() };
			auto sum_of_radii {get_radius() + other.get_radius()};
			return distance_between_centers <= sum_of_radii;
		}

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
		void tick(game::data& wdw) override;
		sf::Vector2f get_position() const override;
		float get_radius() const override;

		bool is_collided(const entity& other) const override;

	};

	export class asteroid : public entity {
		thread_safe::shape<sf::CircleShape>		m_shape;
		std::atomic<int>						m_size;
		std::atomic_flag						m_expired;

		static constexpr float size_scale_factor = 10;
		static constexpr float speed_scale_factor = 0.2f;

	public:

		

		explicit asteroid(sf::Vector2f initial_position, sf::Angle initial_angle, int initial_size) 
			: entity{ sf::Vector2f{speed_scale_factor * max_speed, 0}.rotatedBy(initial_angle) }, m_shape{ static_cast<float>(size_scale_factor * initial_size) }, m_size{ initial_size }, m_expired{} {

			m_shape.set_position(initial_position);
			m_shape.set_origin({ m_shape.get_radius(), m_shape.get_radius() });
		}

		asteroid(const asteroid& other) : entity{ other.m_vel }, m_shape { other.m_shape }, m_size{ other.m_size.load()}, m_expired{} {
			if (other.m_expired.test()) {
				m_expired.test_and_set();
			}
		}
		asteroid& operator=(const asteroid& other) = default;

		asteroid(asteroid&&) noexcept = default;
		asteroid& operator=(asteroid&&) noexcept = default;

		void draw(game::data& wdw) const override;
		bool is_expired() const override;
		void tick(game::data& wdw) override;
		sf::Vector2f get_position() const override;
		float get_radius() const override;
		
		//Kill this asteroid and spawn two others one size smaller
		void on_collision(game::data& dat);

	};



	export class player : public entity {
		thread_safe::shape<sf::ConvexShape> m_shape;

		sf::Vector2f m_accel;

		static sf::ConvexShape create_player();
		static constexpr auto time_between_shots{ std::chrono::milliseconds{300} };
		static constexpr float speed_scale_factor{ 1.0f };
		static constexpr float initial_speed{ 30 };
		static constexpr auto turn_angle{ sf::degrees(3) };

		enum class move_state : std::uint8_t {
			forward_down	= 0b0000'0001,
			backward_down	= 0b0000'0010,
			left_down		= 0b0000'0100,
			right_down		= 0b0000'1000,
			shoot_down		= 0b0001'0000
		};

		friend constexpr bool operator&(move_state lhs, move_state rhs) {
			return std::to_underlying(lhs) & std::to_underlying(rhs);
		}

		//I'd much prefer our movement to formally use an enum than the underlying type
		//but std::atomic only has overloads in the actual integer types
		std::atomic<std::underlying_type_t<move_state>> m_movement{ 0 };
		void set(move_state);
		void clear(move_state);

		void rotate(float in);
		void shoot(game::data&);


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
		void tick(game::data&) override;
		sf::Vector2f get_position() const override;
		float get_radius() const override;

		//To track smooth movement we can't just perform a single action
		//on a single keypress. We instead need to track the current state
		//of the key and act accordingly.
		//This is an OS-level restriction so we can't do much about it.
		void forward_down();
		void forward_up();
		void backward_down();
		void backward_up();
		void left_down();
		void left_up();
		void right_down();
		void right_up();
		void shoot_down();
		void shoot_up();


	};



}