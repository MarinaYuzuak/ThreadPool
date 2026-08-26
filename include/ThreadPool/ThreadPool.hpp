#pragma once

#include <atomic>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <algorithm>

/*
 * SUMMARY
 * Пул потоков забирает себе во владение и переданную таску, и аргументы переданной таски.
 * Это означает, что без явного указания намерения будут созданы копии аргументов/аргументы будут перемещены - т.е
 * отсутствует зависимость от времени жизни объектов в коде, использующим пул. Концептуально довольно похоже на то,
 * как ведет себя std::thread. Чтобы явно передать внутрь рабочего потока ссылку на объект - используется std::ref.
 */
class ThreadPool
{
public:
    ThreadPool(unsigned queueSize = 1, unsigned threadsCount = std::max(1u, std::thread::hardware_concurrency()))
        : m_queueSize(queueSize)
    {
        try
        {
            for(auto i = 0u; i < threadsCount; ++i)
                m_workerThreads.emplace_back(&ThreadPool::doWork, this);
        }
        catch (...)
        {
            stopWork();
            throw;
        }
    }

    ~ThreadPool()
    {
        stopWork();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator= (const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator= (ThreadPool&&) = delete;

    template <typename F, typename... Args>
    std::future<std::invoke_result_t<F, Args...>> Submit(F&& f, Args&&... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvProducer.wait(lock, [this] { return m_isDone || m_taskQueue.size() < m_queueSize; });

        using ResultType = std::invoke_result_t<F, Args...>;
        if(m_isDone)
            return std::future<ResultType>();

        auto task = std::make_shared<std::packaged_task<ResultType()>>(
        [
            func = std::forward<F>(f),
            args = std::tuple<std::decay_t<Args>...>(
                std::forward<Args>(args)...
            )
        ] () mutable
        {
            std::apply(
               [&func](auto&&... unpacked)
               {
                   std::invoke(
                       std::move(func),
                       std::move(unpacked)...
                   );
               },
               std::move(args)
           );
        });

        m_taskQueue.emplace([task] { (*task)(); });

        lock.unlock();

        m_cvConsumer.notify_one();

        return task.get_future();
    }

private:
    std::queue<std::function<void()>> m_taskQueue;
    unsigned m_queueSize = 1;

    std::condition_variable m_cvProducer;
    std::condition_variable m_cvConsumer;
    std::mutex m_mutex;

    std::vector<std::thread> m_workerThreads;
    std::atomic_bool m_isDone = false;

    void stopWork()
    {
        m_isDone = true;
        
        m_cvProducer.notify_all();
        m_cvConsumer.notify_all();

        for(auto&& t : m_workerThreads)
            if(t.joinable())
                t.join();
    }

    void doWork()
    {
        while(true)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cvConsumer.wait(lock, [this] { return  m_isDone || !m_taskQueue.empty(); });

            if(m_isDone && m_taskQueue.empty()) return;
        
            auto task = std::move(m_taskQueue.front());
            m_taskQueue.pop();

            lock.unlock();

            task();

            m_cvProducer.notify_one();
        }
    }
};