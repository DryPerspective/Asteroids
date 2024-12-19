module;

//In theory not necessarily since it's in the global fragment elsewhere, but MSVC complains
#include "SFML/Graphics.hpp"

module game_entities;

import ts_prng;

//We share a generator for all game entities
//In the general case this might have issues, but here we are confident that the lifetime of the generator
//will be approximately equal to the lifetime of the program.
thread_safe::random_generator<float> prng_gen{};


namespace game {

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
		m_shape.move(m_vel * tick_rate);
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
				std::println("Collision detected");
				ent->on_collision(wdw);
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

	bool projectile::is_collided(const game::entity& ent) const {
		//A projectile always travels front-first and the front is the first part which will collide
		//We can refine our collision function for it
		auto front = m_shape.get_transform().transformPoint(m_shape.get_point(0));
		return (front - ent.get_position()).length() <= ent.get_radius();
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
		m_entities.emplace_back(std::in_place_type<game::projectile>, position, rotation );
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
		std::unique_ptr<asteroid> dummy{ nullptr };
		while (m_incoming_asteroids.try_pop(dummy)) {
			m_asteroids.push_back(std::move(dummy));
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