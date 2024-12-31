export module ts_prng;

import std;

namespace thread_safe {

	template<typename Num>
	concept prng_suitable_numerical = std::integral<Num> || std::floating_point<Num>;

	//We need to be able to generate random numbers in a thread_safe context
	//This raises some questions and a few options were considered, but ultimately
	//this was the design which was the best of the bunch
	export template<std::uniform_random_bit_generator Generator = std::mt19937_64>
	class uniform_generator {

		Generator m_gen;
		std::mutex m_mut{};

	public:
		uniform_generator() : m_gen{ std::random_device{}() } {}
		uniform_generator(Generator&& gen) : m_gen{ std::move(gen) } {}

		//It's suboptimal that we have to lock on every random number generation, even that across different ranges
		//But it's worse to spin a fresh generator for every range requested by the user
		template<prng_suitable_numerical ValType = int>
		auto operator()(ValType min, ValType max) {
			auto _{ std::lock_guard{m_mut} };
			//Distributions are cheap and (mostly) stateless, so can be created and destroyed on the fly
			//Pedantically this may affect randomness a little, but this isn't the kind of simulation where that's too important
			if constexpr (std::integral<ValType>) {
				std::uniform_int_distribution<ValType> dist{ min, max };
				return dist(m_gen);
			}
			else {
				std::uniform_real_distribution<ValType> dist{ min, max };
				return dist(m_gen);
			}
		}
		template<prng_suitable_numerical ValType = int>
		auto operator()(ValType max) {
			return operator()(static_cast<ValType>(0), max);
		}

	};


}