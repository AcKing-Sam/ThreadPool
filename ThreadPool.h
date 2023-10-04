#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <future>



class ThreadPool {
public:
    ThreadPool(size_t threads) {
        stall = false;
        for(int i = 0;i < threads;i ++) {
            workers.emplace_back(
                [this]()
                {
                    while(true) 
                    {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(this -> mu);
                            this -> con.wait(lock, [this]{return this -> stall or this -> tasks.size();});
                            if(this -> stall and this -> tasks.size() == 0) 
                                return;
                            task = std::move(this -> tasks.front());
                            this -> tasks.pop();
                        }
                        task();
                    }
                }
            );
        }
    }

    ~ThreadPool() {
        // ensure atomic
        {
            std::unique_lock<std::mutex> lock(mu);
            stall = true;
        }
        con.notify_all();
        // ensure all tasks have beed completed
        for(auto& w : workers) {
            w.join();
        }
    }
    // add one task F(Args) {} into tasks 
    template<class F, class... Args>
    auto addTask(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(mu);

            // don't allow enqueueing after stopping the pool
            if(stall)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace(
                [task]()
                { 
                    (*task)(); 
                }
            );
        }

        con.notify_one();
        return res;
    }

private:
    // worker threads
    std::vector< std::thread > workers;
    // task queue
    std::queue< std::function<void()> > tasks;
    // synchronization
    std::mutex mu;
    std::condition_variable con;
    bool stall;
};


#endif