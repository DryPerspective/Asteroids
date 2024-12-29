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
		template<typename T> requires std::derived_from<T, sf::Drawable>
		void draw(const T& data) {
			auto _{ std::lock_guard{m_mut} };
			m_window.draw(data);
		}



	};
}


template<typename T, typename WindowType = sf::RenderWindow>
concept drawable = requires(T t, WindowType wdw) {
	wdw.draw(t);
};

//A few sense checks
static_assert(drawable<thread_safe::shape<sf::CircleShape>, thread_safe::window>);
static_assert(drawable<sf::Text, thread_safe::window>);


namespace game {

	constexpr inline int score_per_asteroid = 100;

	class data;
	
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

		//If I could choose I wouldn't be doing it this way, but I can't.
		//Our hands are tied by the design choices of SFML
		template<typename T>
		void default_set_position(T& entity, sf::Vector2f new_pos) {
			entity.set_position(new_pos);
		}

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
		virtual void set_position(sf::Vector2f) = 0;

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
		void set_position(sf::Vector2f new_pos) override;
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
		void set_position(sf::Vector2f new_pos) override;
		float get_radius() const override;
		
		//Kill this asteroid and spawn two others one size smaller
		void on_collision(game::data& dat);

	};

	class text : public entity {

		//This throws on failure, but we don't have a lot of good options if we do fail
		sf::Font									m_font{ "assets/PressStart2P-vaV7.ttf" };
		thread_safe::shape<sf::Text>				m_text;
	protected:
		std::atomic_flag		m_expired{};

	public:

		text(sf::String in_str) : entity{ {0,0} }, m_text { m_font, in_str } {}
		text(sf::Vector2f in_vel, sf::String in_str) : entity{ in_vel }, m_text{ m_font, in_str } {}

		text(const text& other) : entity{ other.m_vel }, m_font { other.m_font }, m_text{ m_font, other.m_text.get_string(), other.get_character_size()} {
			m_text.set_position(other.m_text.get_position());
		}
		text& operator=(const text& other) {
			auto _{ std::scoped_lock{m_mut, other.m_mut} };
			m_font = other.m_font;
			m_text = sf::Text{ m_font, other.m_text.get_string() };
			if (other.m_expired.test()) {
				m_expired.test_and_set();
			}
		}

		text(text&&) noexcept = default;
		text& operator=(text&&) noexcept = default;

		void draw(game::data& wdw) const override;
		bool is_expired() const override;
		void tick(game::data& wdw) override;
		sf::Vector2f get_position() const override;
		void set_position(sf::Vector2f new_pos) override;
		float get_radius() const override;
		constexpr bool is_collided(const entity& other) const override { 
			//Text doesn't experience collision
			return false;
		}

		virtual unsigned int get_character_size() const;
		virtual void set_character_size(unsigned int);
		sf::FloatRect get_global_bounds() const;
		void set_string(sf::String in);
		sf::String get_string() const;


	};

	class temp_text : public text {
		std::chrono::duration<double>						m_lifetime_ms;
		std::chrono::high_resolution_clock::time_point		m_start_time;

	public:
		temp_text(std::chrono::duration<double> lifetime_ms, sf::Vector2f vel, sf::String in_text)
			: text{ vel, in_text }, m_lifetime_ms{ lifetime_ms }, m_start_time{ std::chrono::high_resolution_clock::now() } {}

		void tick(game::data&) override;

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
		void set_position(sf::Vector2f new_position) override;
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






	//Not 100% sold on this design, but it provides an easy way to encapsulate the core elements which ultimately other threads are going to need to be able to access
	export class data {

		class game_over_screen {
			game::text m_game_over_text{ "GAME OVER" };
			//More may be added later

		public:
			game_over_screen() = default;

			void set_position(sf::Vector2f new_pos) {
				m_game_over_text.set_position(new_pos);
			}
			sf::Vector2f get_position() const {
				return m_game_over_text.get_position();
			}
			void set_character_size(unsigned int new_size) {
				m_game_over_text.set_character_size(new_size);
			}
			void draw(game::data& dat) {
				m_game_over_text.draw(dat);
			}
		};





		//Asteroids are a special case because only they can collide
		//Everything else is stored here
		thread_safe::vector<polymorphic<game::entity>>	m_entities;
		thread_safe::queue<polymorphic<game::entity>>	m_incoming_entities;


		thread_safe::window								m_window;

		//The only collisions which exist are with asteroids
		//So we keep a separate tally of the asteroids in the simulation. This gives us two benefits
		//The first is that we don't check collisions between things which are never going to collide anyway
		//The second is it allows us to easily avoid locks when scanning down the list of entities from within the list of entities.
		thread_safe::vector<std::unique_ptr<asteroid>> m_asteroids;
		thread_safe::queue<std::unique_ptr<asteroid>>  m_incoming_asteroids;

		std::atomic<int>							   m_game_score{0};
		game::text									   m_score_object{ get_score_string(0) };

		//May change to a enum for multiple gamestates later
		std::atomic_flag							   m_game_over{};
		std::optional<game_over_screen>				   m_game_over_screen{ std::nullopt };


		//Turn a score number into an appropriate string to display
		std::string get_score_string(int score) const;

		//Tick specifically the asteroids and game entities
		void tick_entities();

	public:

		explicit data(sf::RenderWindow&& wdw) : m_window{ std::move(wdw) } {
			m_score_object.set_character_size(20);
			auto [top_left, size] = m_score_object.get_global_bounds();
			auto [bounds_x, bounds_y] = m_window.get_size();
			//We nudge it downwards a tiny bit from the top of the screen, just to prevent it clipping into the window
			m_score_object.set_position({ bounds_x - size.x - 5, 5 });
		}

		//ENTITY MANAGEMENT-------------------------------------------

		void add_projectile(sf::Vector2f position, sf::Angle rotation);
		void add_asteroid();
		void add_asteroid(asteroid&& in);
		void kill_expired();

		void draw_all();


		void draw_entity(const sf::Shape& entity);
		template<typename T> requires drawable<T, thread_safe::window>
		void draw_entity(const T& shp) {
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

		void add_entity(polymorphic<game::entity>&& new_entity);

		void tick();

		void add_score(int in);

		void game_over();
		bool game_is_over() const;

	};



}