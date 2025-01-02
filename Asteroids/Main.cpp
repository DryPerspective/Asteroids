#include <SFML/Graphics.hpp>
#include <SFML/Graphics/Text.hpp>



import ts_queue;
import ts_vector;
import game_entities;


import std;


/*
*  Let's talk threading model. There are a few threads of execution active in the program at once:
*  The main thread: Serves as a rendering thread and is ostensibly the game's clock. We don't want to tie too much to this.
*  The player thread: Processes player input as instructions to the player sprite
*  Asteroid spawning thread: Occasionally throws an asteroid at the screen
*/

constexpr std::ptrdiff_t number_of_threads{ 3 };
//We maintain a (high) tick limit to keep things smooth.
//Uncapped limits can lead to unpredictable behaviour if left unchecked
constexpr std::ptrdiff_t ticks_per_second{ 500 };
constexpr auto tick_interval{ std::chrono::milliseconds{1000} / ticks_per_second };

enum class player_keys {
    none,
    forward_pressed,
    forward_released,
    backward_pressed,
    backward_released,
    left_pressed,
    left_released,
    right_pressed,
    right_released,
    shoot_pressed,
    shoot_released,
    eof
};

player_keys map_player_keypress(sf::Keyboard::Scan in){
    switch (in) {
    case sf::Keyboard::Scan::W:
    case sf::Keyboard::Scan::Up:
        return player_keys::forward_pressed;
    case sf::Keyboard::Scan::S:
    case sf::Keyboard::Scan::Down:
        return player_keys::backward_pressed;
    case sf::Keyboard::Scan::A:
    case sf::Keyboard::Scan::Left:
        return player_keys::left_pressed;
    case sf::Keyboard::Scan::D:
    case sf::Keyboard::Scan::Right:
        return player_keys::right_pressed;
    case sf::Keyboard::Scan::Space:
        return player_keys::shoot_pressed;
    default:
        return player_keys::none;
    }
}

player_keys map_player_key_release(sf::Keyboard::Scan in) {
    switch (in) {
    case sf::Keyboard::Scan::W:
    case sf::Keyboard::Scan::Up:
        return player_keys::forward_released;
    case sf::Keyboard::Scan::S:
    case sf::Keyboard::Scan::Down:
        return player_keys::backward_released;
    case sf::Keyboard::Scan::A:
    case sf::Keyboard::Scan::Left:
        return player_keys::left_released;
    case sf::Keyboard::Scan::D:
    case sf::Keyboard::Scan::Right:
        return player_keys::right_released;
    case sf::Keyboard::Scan::Space:
        return player_keys::shoot_released;
    default:
        return player_keys::none;
    }
    
}


void translate_keypress_to_player_action(game::player& player, player_keys keypress) {
    switch (keypress) {
    case player_keys::forward_pressed:
        player.forward_down();
        break;
    case player_keys::backward_pressed:
        player.backward_down();
        break;
    case player_keys::forward_released:
        player.forward_up();
        break;
    case player_keys::backward_released:
        player.backward_up();
        break;
    case player_keys::left_pressed:
        player.left_down();
        break;
    case player_keys::left_released:
        player.left_up();
        break;
    case player_keys::right_pressed:
        player.right_down();
        break;
    case player_keys::right_released:
        player.right_up();
        break;
    case player_keys::shoot_pressed:
        player.shoot_down();
        break;
    case player_keys::shoot_released:
        player.shoot_up();
    }
}







int main()
{
    unsigned int win_x{ 500 };
    unsigned int win_y{ 500 };

    sf::RenderWindow main_window{ sf::VideoMode({win_x, win_y}), "Asteroids" };
    main_window.setFramerateLimit(120);

    game::data game_data{ std::move(main_window) };
    game::player player({ 100, 100 }, game_data);

    thread_safe::queue<player_keys> control_input{};

    std::latch starting_line{ number_of_threads };

    std::jthread game_process_thread{ [&](std::stop_token tok) {
        starting_line.arrive_and_wait();
        while (!tok.stop_requested()) {
            player_keys key{};
            //If we're in time for a tick
            control_input.wait_pop(key);
            if (key == player_keys::eof) return;
            translate_keypress_to_player_action(player, key);
        }
    } };

    std::jthread spawn_asteroids{ [&game_data, &starting_line](std::stop_token tok) {
        std::mt19937 mt{ std::random_device{}() };
        std::uniform_int_distribution dist{ 1000, 2000 };
        starting_line.arrive_and_wait();
        while (!tok.stop_requested() && !game_data.game_is_over()) {
            game_data.add_asteroid();
            std::this_thread::sleep_for(std::chrono::milliseconds{ dist(mt) });
        }
    }};

    std::uint8_t tick_count{};

    starting_line.arrive_and_wait();
    auto loop_start = std::chrono::steady_clock::now();

    while (++tick_count, game_data.is_open()) {
        
        while (const auto event = game_data.poll_event()) {
            if (event->getIf<sf::Event::Closed>()) {
                control_input.push(player_keys::eof);
                game_data.close();
                break;
            }
            if (auto key = event->getIf<sf::Event::KeyPressed>()) {
                control_input.push(map_player_keypress(key->scancode));
            }
            if (auto key = event->getIf<sf::Event::KeyReleased>()) {
                control_input.push(map_player_key_release(key->scancode));
            }

            
        }
        
        

        game_data.clear(sf::Color::Black);

        
        player.draw(game_data);


        game_data.draw_all();

        game_data.display();

        if (std::chrono::steady_clock::now() - loop_start > tick_interval) {
            loop_start = std::chrono::steady_clock::now();

            player.tick(game_data);
            game_data.kill_expired();
            game_data.tick();
        }

        //std::println("{}", dat.num_entities());



    }

    return 0;
}