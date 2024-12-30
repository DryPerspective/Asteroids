module;

//In theory not necessarily since it's in the global fragment elsewhere, but MSVC complains
#include "SFML/Graphics.hpp"

module game_entities;

import ts_prng;
import polymorphic;

//We share a generator for all game entities
//In the general case this might have issues, but here we are confident that the lifetime of the generator
//will be approximately equal to the lifetime of the program.
thread_safe::random_generator<float> prng_gen{};


namespace game {

	class dot : public entity {
		thread_safe::shape<sf::CircleShape> m_shape;

	public:
		dot(sf::Vector2f position) : entity{ { 0, 0 } }, m_shape{ 5.0f } {
			m_shape.set_position(position);
		}

		dot(const dot& other) : entity{ other.m_vel }, m_shape{ other.m_shape } {}

		bool is_expired() const override {
			return false;
		}

		void draw(game::data& dat) const override {
			dat.draw_entity(m_shape);
		};

		//Process a single tick 
		void tick(game::data&) override {};

		//We can still require position is queriable though
		sf::Vector2f get_position() const override {
			return m_shape.get_position();
		}
		void set_position(sf::Vector2f in) override {
			m_shape.set_position(in);
		}

		//And while each shape may be different, acquiring their radii will vastly simplify things
		float get_radius() const override {
			return m_shape.get_radius();
		}
	};

	//FREE FUNCTIONS-------------------------------------------------------------

	bool shape_within_bounds(const game::entity& ent, sf::Vector2u wdw) {

		return shape_within_bounds(ent.get_position(), ent.get_radius(), wdw);

	}

	bool shape_within_bounds(sf::Vector2f pos, float radius, sf::Vector2u wdw) {
		auto [x, y] = pos;
		auto [max_x, max_y] = wdw;

		return not ((x - radius) < 0 || (y - radius) < 0
			|| (x + radius) > max_x || (y + radius > max_y));
	}

	bool collides_with_line(sf::Vector2f point_a, sf::Vector2f point_b, const asteroid& other) {
		//Calculating if any part of the circle intersects a line is workable - we draw an imaginary right-angled triangle
		//from the circle's centre to the line. We then calculate the length of the line from our triangle edge to the circle center
		//If it's smaller than the radius, we have an intersection.
		auto c_vector{ point_a - other.get_position() }; //Hypoteneuse
		auto p_vector{ point_a - point_b };			     //Length of side of triangle. Contains the adjacent but length may differ

		//We know that P dot C = |P||C|cos(theta) where theta is the angle between the hypotenuse and the adjacent
		//We also know that cos(theta) = adjacent / hypoteneuse.
		//So P * C = |P||C| a / |C|
		//Which falls out at a = P * C / |P|
		auto inner_product = [](sf::Vector2f lhs, sf::Vector2f rhs) {
			return (lhs.x * rhs.x) + (lhs.y * rhs.y);
		};
		auto adjacent_length = inner_product(p_vector, c_vector) / p_vector.length();
		
		//Which then means that trusty old pythagoras can get us the length of the opposite
		
		//But, there is a minor hitch. We only care about a circle which subtends that line when it happens between the two points.
		//The line itself may continue to stretch to infinity, but we don't want to track collisions on that infinite line
		
		//First check - recall that a negative inner product means an angle of greater than 90 degrees between the lines.
		//We don't want these
		if (adjacent_length > 0) {
			//Second check - we want to ensure that the opposite we're looking for is within the length of the line
			//This is also simple
			if (adjacent_length < p_vector.length()) {
				//Now we're back to Pythagoras
				if (std::sqrt(c_vector.lengthSquared() - adjacent_length * adjacent_length) <= other.get_radius()) return true;
			}
		}
		return false;
	}

	//ENTITY FUNCTIONS----------------------------------------------------------

	bool entity::is_collided(const asteroid& other) const {
		//The most basic detection is to model our two objects as two circles
		//This may be refined on a per-class basis
		auto distance_between_centers{ (get_position() - other.get_position()).length() };
		auto sum_of_radii{ get_radius() + other.get_radius() };
		return distance_between_centers <= sum_of_radii;
	}

	//PROJECTILE FUNCTIONS------------------------------------------------------
	projectile& projectile::operator=(const projectile& other){
		m_shape = other.m_shape;
		if (other.m_expired.test()) m_expired.test_and_set();
		return *this;
	}

	void projectile::draw(game::data& wdw) const {
		wdw.draw_entity(m_shape);
	}

	bool projectile::is_expired() const {
		return m_expired.test();
	}

	void projectile::tick(game::data& wdw) {
		auto lck{ std::unique_lock{m_mut} };
		m_shape.move(m_vel * tick_rate);
		lck.unlock();
		if (!shape_within_bounds(*this, wdw.window_size())) {
			m_expired.test_and_set();
			return;
		}


		//Let's talk collisions
		//Projectiles are the fastest objects in the game and all move at the same speed
		//As such, they can never collide with the player or each other.
		//So if we get a collision it means it must be with an asteroid
		//For now we're only testing
		auto collision_reaction = [&, this](std::unique_ptr<asteroid>& ent) {
			if (is_collided(*ent.get())) {
				ent->on_collision(wdw);
				
				wdw.add_score(score_per_asteroid);
				temp_text scorecard{ std::chrono::milliseconds{500}, sf::Vector2f{0,-50}, std::to_string(score_per_asteroid)};
				scorecard.set_character_size(10);
				scorecard.set_position(ent->get_position() - sf::Vector2f{30, 0});
				
				wdw.add_entity(polymorphic<entity>{std::move(scorecard)});

				m_expired.test_and_set();
			}
		};
		wdw.for_all_asteroids(collision_reaction);
	}

	sf::Vector2f projectile::get_position() const {
		return m_shape.get_position();
	}

	float projectile::get_radius() const {
		return (m_shape.get_origin() - m_shape.get_point(0)).length();
	}

	sf::Vector2f projectile::get_start_vel(sf::Angle rot) {
		//We want projectiles to travel at the max speed allowed by the simulation, because this
		//allows us to simplify certain things through assumptions which hold because of it
		//e.g. projectiles cannot collide with other projectiles, or the player.
		return sf::Vector2f{ -max_speed, 0 }.rotatedBy(rot);
	}

	bool projectile::is_collided(const game::asteroid& ent) const {
		//A projectile always travels front-first and the front is the first part which will collide
		//We can refine our collision function for it
		auto front = m_shape.get_transform().transformPoint(m_shape.get_point(0));
		return (front - ent.get_position()).length() <= ent.get_radius();
	}

	void projectile::set_position(sf::Vector2f in) {
		default_set_position(m_shape, in);
	}

	//ASTEROID FUNCTIONS-------------------------------------------------------------

	void asteroid::draw(game::data& wdw) const {
		wdw.draw_entity(m_shape);
	}

	bool asteroid::is_expired() const {
		return m_expired.test();
	}

	void asteroid::tick(game::data& wdw) {
		m_shape.move(m_vel * tick_rate);

		//We need to know whether to cull an asteroid. Since they all start offscreen we can't just
		//check if they're offscreen to know when to cull them
		//While we could play games with swapping states of "has been in bounds", a semantically cleaner solution
		//is to cull any asteroids which get more than 10% off the side of the screen.
		auto [bounds_x, bounds_y] = wdw.window_size();
		auto [percent_x, percent_y] = [=](float scale) {
			return std::make_pair(bounds_x * scale, bounds_y * scale);
		}(0.1f);

		auto [pos_x, pos_y] = m_shape.get_position();
		if (pos_x < -percent_x || pos_y < -percent_y
			|| pos_x > bounds_x + percent_x || pos_y > bounds_y + percent_y) {
			m_expired.test_and_set();
		}
		
		
	}

	sf::Vector2f asteroid::get_position() const {
		return m_shape.get_position();
	}

	void asteroid::set_position(sf::Vector2f in) {
		default_set_position(m_shape, in);
	}

	float asteroid::get_radius() const {
		return m_shape.get_radius();
	}

	void asteroid::on_collision(game::data& dat) {
		// This may look racey, but this function is the only one which can
		// alter the size member, and it will not be called concurrently with itself
		auto current_size = m_size.load();
		//If the asteroid is already as small as it's going to be, just kill it
		if (current_size == 1) {
			m_expired.test_and_set();
			return;
		}

		//Get a random phase angle for new asteroids to spawn in
		auto phase{ sf::degrees(prng_gen(0.0f, 180.0f))};
		//Create two new asteroids going in opposite directions
		dat.add_asteroid(asteroid{ get_position(), phase, current_size - 1});
		dat.add_asteroid(asteroid{ get_position(), phase - sf::degrees(180), current_size - 1 });

		//And mark this one as dead
		m_expired.test_and_set();

	}

	

	//PLAYER FUNCTIONS---------------------------------------------------------------
	sf::ConvexShape player::create_player() {

		/*
				2
				/\
			   /  \
			  /    \
			 /   0  \
			/   /\   \
		   /   /  \   \
		  / ///    \\\ \
		  1            3

		*/

		sf::ConvexShape player{};
		player.setPointCount(4);
		player.setPoint(0, { 50, 0 });
		player.setPoint(1, { 60, -30 });
		player.setPoint(2, { 0, 0 });
		player.setPoint(3, { 60, 30 });
		player.setOrigin({ 40, 0 });
		return player;
	}


	void player::rotate(sf::Angle angle) {
		m_shape.rotate(angle);
	}

	void player::set_position(sf::Vector2f new_position) {
		m_shape.set_position(new_position);
	}

	void player::draw(game::data& dat) const {
		dat.draw_entity(m_shape);
	}


	void player::rotate(float in) {
		rotate(sf::degrees(in));
	}

	void player::shoot(game::data& dat) {
		static std::chrono::steady_clock::time_point last_shot{}; //Default ctor sets to the clock's epoch
		if ((std::chrono::steady_clock::now() - last_shot) < time_between_shots) return;

		last_shot = std::chrono::steady_clock::now();
		dat.add_projectile(m_shape.get_transform().transformPoint(m_shape.get_point(2)), m_shape.get_rotation());
	}

	void player::tick(game::data& dat) {

		auto transformed_point = [this](std::size_t idx) {
			return m_shape.get_transform().transformPoint(m_shape.get_point(idx));
		};


		if(dat.game_is_over()) return;

		auto _{ std::lock_guard{m_mut} };

		//The max speed of the player is 95% that of the maximum allowed by the game
		const bool under_max_speed = m_vel.length() <= max_speed * 0.95f;

		//One could argue that this is TOCTOU, but I would prefer each tick to operate on a consistent
		//snapshot of one moment as it is/was at the time the function was called, rather than
		//potentially many states being processed between the call to tick and the function evaluating them.
		const auto movement{ static_cast<move_state>(m_movement.load(std::memory_order_acquire)) };
		//If going forward, we accelerate 
		if (movement & move_state::forward_down && under_max_speed) {
			m_accel += sf::Vector2f{ -10,0 }.rotatedBy(m_shape.get_rotation()) * speed_scale_factor;
		}
		//If backwards we accelerate backwards
		else if (movement & move_state::backward_down && under_max_speed) {
			m_accel += sf::Vector2f{ 10,0 }.rotatedBy(m_shape.get_rotation()) * speed_scale_factor;
		}
		//Otherwise we dampen speed and decelerate
		else if (m_vel.lengthSquared() > 0) {
			m_accel = sf::Vector2f{ -10, 0 }.rotatedBy(m_vel.angle()) * speed_scale_factor * 10.0f;
		}

		//If we're rotating, we rotate
		if (movement & move_state::left_down) {
			m_shape.rotate(-turn_angle);
		}
		else if (movement & move_state::right_down) {
			m_shape.rotate(turn_angle);
		}

		//And if we're shooting, we shoot
		if (movement & move_state::shoot_down) {
			shoot(dat);
		}
		auto new_vel{ m_vel + (m_accel * tick_rate) };
		//Calculate what the new position will be
		auto new_pos { m_shape.get_position() + new_vel * tick_rate };
		//If that position would be inside the bounds of the window
		if (shape_within_bounds(new_pos, get_radius(), dat.window_size())) {
			m_shape.set_position(new_pos);
			m_vel = new_vel;

		}
		//Otherwise
		else {
			//Kill acceleration and velocity in the direction of outside the box
			
			auto [pos_x, pos_y] = new_pos;
			auto [bounds_x, bounds_y] = dat.window_size();
			auto rad{ get_radius() };
			
			if (pos_y - rad < 0 || pos_y + rad > bounds_y) {

				m_vel = { m_vel.x, 0 };
				m_accel = { m_accel.x, 0 };
			}
			else{
				m_vel = { 0, m_vel.y };
				m_accel = { 0, m_accel.y };				
			}
			//Give a very slight nudge towards the centre of the screen
			m_shape.move((sf::Vector2f{ static_cast<float>(bounds_x) / 2, static_cast<float>(bounds_y) /2 } - m_shape.get_position()) * 0.01f);
		}


		//Then process collisions
		auto collision_visitor = [&, this](std::unique_ptr<asteroid>& ent) {
			if (is_collided(*ent.get())) {
				dat.game_over();
			}
		};
		dat.for_all_asteroids(collision_visitor);


	}

	sf::Vector2f player::get_position() const {
		return m_shape.get_position();
	}
	float player::get_radius() const {
		return (m_shape.get_origin() - m_shape.get_point(3)).length();
	}

	void player::set(move_state in) {
		m_movement.fetch_or(std::to_underlying(in), std::memory_order_acq_rel);
	}
	void player::clear(move_state in) {
		//Who doesn't love implicit integer promotions
		auto cleared_value = static_cast<std::underlying_type_t<move_state>>(~std::to_underlying(in));
		m_movement.fetch_and(cleared_value, std::memory_order_acq_rel);		
	}

	//Detecting collisions better
	bool player::is_collided(const asteroid& other) const {
		//We model the overall player model as a triangle. This remains imperfect but is better than a circle
		//May return later and model it as two triangles.
		//In this case, we need our three collision points (1-3)
		auto transformed_point = [this](std::size_t idx) {
			return m_shape.get_transform().transformPoint(m_shape.get_point(idx));
		};
		std::array<sf::Vector2f, 3> points{ transformed_point(1), transformed_point(2), transformed_point(3) };
		//First test - do any points exist within the asteroid?
		for (const auto& point : points) {
			if ((point - other.get_position()).length() < other.get_radius()) return true;
		}
		
		//Otherwise we want to test if any part of the circle subtends any line which makes up our triangle



		//Since we're handspinning we put in a check to make sure future optimizations don't break this
		static_assert(points.size() == 3, "Player collision calculation expects exactly 3 points");

		return
				collides_with_line(points[0], points[1], other)
			||	collides_with_line(points[0], points[2], other)
			||	collides_with_line(points[1], points[2], other);;

	}


	void player::forward_down() {
		set(move_state::forward_down);
	}
	void player::forward_up() {
		clear(move_state::forward_down);
	}

	void player::backward_down() {
		set(move_state::backward_down);
	}
	void player::backward_up() {
		clear(move_state::backward_down);
	}

	void player::left_down() {
		set(move_state::left_down);
	}
	void player::left_up() {
		clear(move_state::left_down);
	}
	void player::right_down() {
		set(move_state::right_down);
	}
	void player::right_up() {
		clear(move_state::right_down);
	}
	void player::shoot_down() {
		set(move_state::shoot_down);
	}
	void player::shoot_up() {
		clear(move_state::shoot_down);
	}


	//DATA FUNCTIONS-----------------------------------------------------------------

	void data::add_projectile(sf::Vector2f position, sf::Angle rotation) {
		m_incoming_entities.push(polymorphic<game::entity>{std::in_place_type<game::projectile>, position, rotation });
	}

	void data::add_asteroid() {
		/*
		*  All asteroids start in a belt around the edge of the existing window.
		*  They then head in a direction which is approximately towards the center of the screen.
		*/
		//First we grab a belt around the edges of the screen
		auto [size_x, size_y] = m_window.get_size();
		//And a position within it
		auto x_rand{ prng_gen(0.0f, 10.0f) };
		//We want them coming from all angles, so we cut the result in half
		float x_pos{};
		if (x_rand < 5) {
			x_pos = -x_rand;
		}
		else {
			x_pos = size_x + x_rand;
		}

		auto y_rand{ prng_gen(0.0f, 10.0f) };
		float y_pos{};
		if (y_rand < 0.5) {
			y_pos = -y_rand;
		}
		else {
			y_pos = size_y + y_rand;
		}

		sf::Vector2f pos{ x_pos, y_pos };

		//Then we want a velocity which points towards the centre of the screen but is peturbed a little
		sf::Vector2f velocity = sf::Vector2f{ static_cast<float>(size_x) / 2, static_cast<float>(size_y) / 2 } - pos.rotatedBy(sf::degrees(prng_gen(-30.0f, 30.0f)));

		m_incoming_asteroids.push(std::make_unique<game::asteroid>(sf::Vector2f{ x_pos, y_pos }, velocity.angle(), 3));

	}

	void data::add_asteroid(asteroid&& in) {
		m_incoming_asteroids.push(std::make_unique<asteroid>(std::move(in)));
	}

	void data::kill_expired() {
		auto eraser = [](auto& entity) {
			return entity->is_expired();
		};
		m_entities.erase_if(eraser);
		m_asteroids.erase_if(eraser);

	}

	void data::draw_all() {
		//Fight lambda rules on capturing members
		auto draw_func{ std::bind_front([](game::data& dat, auto& ent) {
			ent->draw(dat);
		}, std::ref(*this)) };
		m_entities.for_each(draw_func);
		m_asteroids.for_each(draw_func);
		m_score_object.draw(*this);

		if (m_game_over_screen.has_value()) {
			m_game_over_screen->draw(*this);
		}
		
	}

	void data::tick_entities() {
		auto ticker{ std::bind_front([](game::data& dat, auto& ent) {
			ent->tick(dat);
		}, std::ref(*this)) };
		m_entities.for_each(ticker);
		m_asteroids.for_each(ticker);
	}

	std::size_t data::num_entities() const {
		return m_entities.size() + m_asteroids.size();
	}

	void data::draw_entity(const sf::Shape& entity) {
		m_window.draw(entity);
	}

	sf::Vector2u data::window_size() const {
		return m_window.get_size();
	}

	bool data::is_open() const {
		return m_window.is_open();
	}

	void data::close() {
		m_window.close();
	}

	std::optional<sf::Event> data::poll_event() {
		return m_window.poll_event();
	}

	void data::display() {
		m_window.display();
	}
	void data::clear(sf::Color colour) {
		m_window.clear(colour);
	}

	void data::tick() {
		//If the game is over, we stop processing and 
		if (m_game_over.test()) {
			if (not m_game_over_screen.has_value()) {
				m_game_over_screen.emplace(game_over_screen{});
				m_game_over_screen->set_character_size(30);
				auto screen_middle = [](thread_safe::window& wdw) {
					auto [bounds_x, bounds_y] = wdw.get_size();
					return sf::Vector2f{ bounds_x / 2.0f, bounds_y / 2.0f };
				}(m_window);
				//Adjust per the size of the characters we have
				screen_middle.x -= 140;
				screen_middle.y -= 15;
				m_game_over_screen->set_position(screen_middle);
			}
		}
		else {
			std::unique_ptr<asteroid> dummy{ nullptr };
			while (m_incoming_asteroids.try_pop(dummy)) {
				m_asteroids.push_back(std::move(dummy));
			}
			//We need a dummy entity to assign to
			polymorphic<entity> dummy_entity{ std::in_place_type<projectile>, sf::Vector2f{0,0}, sf::degrees(0) };
			while (m_incoming_entities.try_pop(dummy_entity)) {
				m_entities.push_back(std::move(dummy_entity));
			}
			m_score_object.set_string(get_score_string(m_game_score.load(std::memory_order_acquire)));

			tick_entities();

		}
	}

	void data::add_entity(polymorphic<game::entity>&& new_entity) {
		m_incoming_entities.push(std::move(new_entity));
	}

	std::string data::get_score_string(int score) const {
		return std::format("Score: {:0>5}", score);
	}

	void data::add_score(int in) {
		m_game_score.fetch_add(in, std::memory_order_acq_rel);
	}

	void data::game_over() {
		m_game_over.test_and_set();
	}
	bool data::game_is_over() const {
		return m_game_over.test();
	}

	//TEXT---------------------------------------------------------------------
	void text::draw(game::data& wdw) const {
		auto _{ std::shared_lock{m_mut} };
		wdw.draw_entity(m_text);
	}
	bool text::is_expired() const {
		return m_expired.test();
	}
	void text::tick(game::data& wdw) {
		auto lck{ std::lock_guard{m_mut} };
		m_text.move(m_vel * tick_rate);
		if (!shape_within_bounds(*this, wdw.window_size())) {
			m_expired.test_and_set();
			return;
		}
	}
	sf::Vector2f text::get_position() const {
		return m_text.get_position();
	}
	void text::set_position(sf::Vector2f in) {
		default_set_position(m_text, in);
	}
	float text::get_radius() const {
		return static_cast<float>(m_text.get_character_size());
	}

	unsigned int text::get_character_size() const {
		return m_text.get_character_size();
	}
	void text::set_character_size(unsigned int in) {
		m_text.set_character_size(in);
	}

	sf::FloatRect text::get_global_bounds() const {
		return m_text.get_global_bounds();
	}

	void text::set_string(sf::String in) {
		m_text.set_string(in);
	}
	sf::String text::get_string() const {
		return m_text.get_string();
	}

	//TEMP-TEXT---------------------------------------------------
	void temp_text::tick(game::data& wdw) {
		text::tick(wdw);	
		auto _{ std::lock_guard{m_mut} };
		if (std::chrono::high_resolution_clock::now() - m_start_time >= m_lifetime_ms) {
			m_expired.test_and_set();
		}
		
	}

}

namespace thread_safe {
	//TS_WINDOW---------------------------------------------------------------------------------------------------------
	void window::draw(const sf::Shape& to_draw) {
		auto _{ std::lock_guard{m_mut} };
		m_window.draw(to_draw);
	}

	void window::clear(sf::Color colour) {
		auto _{ std::lock_guard{m_mut} };
		m_window.clear(colour);
	}

	void window::display() {
		auto _{ std::lock_guard{m_mut} };
		m_window.display();
	}

	sf::Vector2u window::get_size() const {
		auto _{ std::lock_guard{m_mut} };
		return m_window.getSize();
	}

	bool window::is_open() const {
		auto _{ std::lock_guard{m_mut} };
		return m_window.isOpen();
	}
	void window::close() {
		auto _{ std::lock_guard{m_mut} };
		m_window.close();
	}
	std::optional<sf::Event> window::poll_event() {
		auto _{ std::lock_guard{m_mut} };
		return m_window.pollEvent();
	}
}
