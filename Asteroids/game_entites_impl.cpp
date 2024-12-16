module;

//In theory not necessarily since it's in the global fragment elsewhere, but MSVC complains
#include "SFML/Graphics.hpp"

module game_entities;

import ts_shape;


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

	void projectile::tick(const game::data& wdw) {
		m_shape.move(m_vel * tick_rate);
		if (!shape_within_bounds(*this, wdw.window_size())) {
			m_expired.test_and_set();
		}
	}

	sf::Vector2f projectile::get_position() const {
		return m_shape.get_position();
	}

	float projectile::get_radius() const {
		return (m_shape.get_origin() - m_shape.get_point(0)).length();
	}

	sf::Vector2f projectile::get_start_vel(sf::Angle rot) {
		//We want a velocity for which a projectile takes 2 seconds to cross the screen.
		//Each tick, it moves vel * tick_rate
		return sf::Vector2f{ -2 / tick_rate, 0 }.rotatedBy(rot);
	}


	//ASTEROID FUNCTIONS-------------------------------------------------------------

	void asteroid::draw(game::data& wdw) const {
		wdw.draw_entity(m_shape);
	}

	bool asteroid::is_expired() const {
		return m_expired.test();
	}

	void asteroid::tick(const game::data& wdw) {
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


	void player::move(float quantity, const game::data& dat) {
		auto _{ std::lock_guard{m_mut} };
		/*
		auto pos = m_shape.get_position() + sf::Vector2f{ -quantity, 0 }.rotatedBy(m_shape.get_rotation());
		if (shape_within_bounds(pos, get_radius(), dat.window_size())) {
			m_shape.set_position(pos);
		}
		*/
		m_accel += sf::Vector2f{ -quantity,0 }.rotatedBy(m_shape.get_rotation()) * speed_scale_factor;
	}

	void player::rotate(float in) {
		rotate(sf::degrees(in));
	}

	void player::shoot(game::data& dat) {
		auto _{ std::lock_guard{m_mut} };
		static std::chrono::steady_clock::time_point last_shot{}; //Default ctor sets to the clock's epoch
		if ((std::chrono::steady_clock::now() - last_shot) < time_between_shots) return;

		last_shot = std::chrono::steady_clock::now();
		dat.add_projectile(m_shape.get_transform().transformPoint(m_shape.get_point(2)), m_shape.get_rotation());
	}

	void player::tick(const game::data& dat) {
		auto _{ std::lock_guard{m_mut} };
		auto new_vel{ m_vel + (m_accel * tick_rate) };
		//Calculate what the new position will be
		auto new_pos { m_shape.get_position() + new_vel * tick_rate };
		//If that position would be inside the bounds of the window
		if (shape_within_bounds(new_pos, get_radius(), dat.window_size())) {
			//Update stats and dampen acceleration
			m_shape.set_position(new_pos);
			m_vel = new_vel;
			if (m_vel.lengthSquared() > std::numeric_limits<float>::epsilon()) {
				m_accel = m_accel * -0.75f;				
			}
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
		static std::mt19937_64 mt{ std::random_device{}() };
		static std::uniform_real_distribution<float> pos_dist{0.0f, 10.0f};
		auto x_rand{ pos_dist(mt) };
		//We want them coming from all angles, so we cut the result in half
		float x_pos{};
		if (x_rand < 5) {
			x_pos = -x_rand;
		}
		else {
			x_pos = size_x + x_rand;
		}

		auto y_rand{ pos_dist(mt) };
		float y_pos{};
		if (y_rand < 0.5) {
			y_pos = -y_rand;
		}
		else {
			y_pos = size_y + y_rand;
		}

		sf::Vector2f pos{ x_pos, y_pos };

		//Then we want a velocity which points towards the centre of the screen but is peturbed a little
		static std::uniform_int_distribution vel_dist{ -30, 30 };
		sf::Vector2f velocity = sf::Vector2f{ static_cast<float>(size_x) / 2, static_cast<float>(size_y) / 2 } - pos.rotatedBy(sf::degrees(static_cast<float>(vel_dist(mt))));
		//Scale initial velocity
		velocity *= asteroid::speed_scale_factor;

		m_entities.emplace_back(std::in_place_type<game::asteroid>, sf::Vector2f{ x_pos, y_pos }, velocity, 3 );

	}

	void data::kill_expired() {
		m_entities.erase_if([](polymorphic<game::entity>& ent) {
			return ent->is_expired();
		});
	}

	void data::draw_all() {
		//Fight lambda rules on capturing members
		m_entities.for_each(std::bind_front([](game::data& dat, polymorphic<game::entity>& ent) {
			ent->draw(dat);
		}, std::ref(*this)));
	}

	void data::tick_all() {
		m_entities.for_each(std::bind_front([](game::data& dat, polymorphic<game::entity>& ent) {
			ent->tick(dat);
		}, std::ref(*this)));
	}

	std::size_t data::num_entities() const {
		return m_entities.size();
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