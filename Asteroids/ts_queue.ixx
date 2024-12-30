export module ts_queue;

import std;


//I don't like out paramters in the general case, but I concede that they do solve
//a rather irritating problem of race conditions and exception safety here.
//But if I must use them I'd rather my code be explicit about it.

template<typename T>
using out_param = T&;


namespace thread_safe {
    export template<typename T>
    class queue {
        std::queue<T>               m_dat;

        mutable std::mutex          m_mut;
        std::condition_variable     m_cond;


    public:
        queue() = default;
        queue(const queue& rhs) {
            auto _{ std::lock_guard{rhs.m_mut} };
            m_dat = rhs.m_dat;
        }

        queue& operator=(const queue& rhs) {
            if (this != &rhs) {
                auto _{ std::scoped_lock{m_mut, rhs.m_mut} };
                m_dat = rhs.m_dat;
            }
            return *this;
        };


        void push(T in_val) {
            auto _{ std::lock_guard{m_mut} };
            m_dat.push(std::move(in_val));
            m_cond.notify_one();
        }

        //Bind to a given value via an out-param
        //Avoids awkward race conditions
        //Wait_pop to block until there is a value to get
        void wait_pop(out_param<T> in_val) {
            auto lck{ std::unique_lock{m_mut} };
            m_cond.wait(lck, [this]() {return !m_dat.empty(); });
            in_val = std::move(m_dat.front());
            m_dat.pop();
        }

        //Try_pop to succeed or fail getting a value without waiting
        bool try_pop(out_param<T> in_val) {
            auto _{ std::lock_guard{m_mut} };
            if (m_dat.empty()) return false;
            in_val = std::move(m_dat.front());
            m_dat.pop();
            return true;
        }

        bool empty() const {
            auto _{ std::lock_guard{m_mut} };
            return m_dat.empty();
        }

    };
}