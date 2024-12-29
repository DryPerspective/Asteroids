#include <SFML/Graphics.hpp>
#include <SFML/Graphics/Text.hpp>



import ts_queue;
import ts_vector;
import game_entities;


import std;


/*
*  Let's talk threading model. There are a few threads of execution active in the program at once:
*  The main thread: Serves as a rendering thread and is ostensibly the game's clock. We don't want to tie too much to this.
*  The player/tick thread: Keeps the entities up to date and processes player input.
*  Asteroid spawning thread: Occasionally throws an asteroid at the screen
*/

constexpr std::ptrdiff_t number_of_threads{ 3 };

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






class input_processor {
    thread_safe::queue<player_keys>&     m_queue;
    game::player&                               m_player;
    game::data&                                 m_data;
    

public:


    explicit input_processor(thread_safe::queue<player_keys>& in_q, game::player& in_c, game::data& in_dat) : m_queue{ in_q }, m_player{ in_c }, m_data{ in_dat } {}

    void operator()(std::stop_token tok) {
        while (!tok.stop_requested()) {
            player_keys code{};
            m_queue.wait_pop(code);

            switch (code) {
            case player_keys::eof:
                return;
            case player_keys::forward_pressed:
                m_player.forward_down();
                break;
            case player_keys::backward_pressed:
                m_player.backward_down();
                break;
            case player_keys::forward_released:
                m_player.forward_up();
                break;
            case player_keys::backward_released:
                m_player.backward_up();
                break;
            case player_keys::left_pressed:
                m_player.left_down();
                break;
            case player_keys::left_released:
                m_player.left_up();
                break;
            case player_keys::right_pressed:
                m_player.right_down();
                break;
            case player_keys::right_released:
                m_player.right_up();
                break;
            case player_keys::shoot_pressed:
                m_player.shoot_down();
                break;
            case player_keys::shoot_released:
                m_player.shoot_up();
            }



        }
    }

};







int main()
{
    unsigned int win_x{ 500 };
    unsigned int win_y{ 500 };

    sf::RenderWindow main_window{ sf::VideoMode({win_x, win_y}), "My Window" };
    main_window.setFramerateLimit(120);
    //main_window.setKeyRepeatEnabled(false);

    game::data dat{ std::move(main_window) };
    game::player p({ 100, 100 }, dat);

    thread_safe::queue<player_keys> control_input{};
    std::jthread input_thread{ input_processor{ control_input, p, dat} };

    std::jthread spawn_asteroids{ [&dat](std::stop_token tok) {
        std::mt19937 mt{ std::random_device{}() };
        std::uniform_int_distribution dist{ 1000, 2500 };
        while (!tok.stop_requested()) {
            dat.add_asteroid();
            std::this_thread::sleep_for(std::chrono::milliseconds{ dist(mt) });
        }
    }};

    std::uint8_t tick_count{};

    while (++tick_count, dat.is_open()) {
        while (const auto event = dat.poll_event()) {
            if (event->getIf<sf::Event::Closed>()) {
                control_input.push(player_keys::eof);
                dat.close();
                break;
            }
            if (auto key = event->getIf<sf::Event::KeyPressed>()) {
                control_input.push(map_player_keypress(key->scancode));
            }
            if (auto key = event->getIf<sf::Event::KeyReleased>()) {
                control_input.push(map_player_key_release(key->scancode));
            }

            
        }

        

        dat.clear(sf::Color::Black);

        p.tick(dat);
        p.draw(dat);   
        
        dat.kill_expired();
        dat.tick();
        
        dat.draw_all();
        
        dat.display();


        std::println("{}", dat.num_entities());

    }

    return 0;
}