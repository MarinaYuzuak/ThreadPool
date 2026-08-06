#pragma once

#include <atomic>
#include <condition_variable>
#include <queue>
#include <functional>
#include <mutex>
#include <thread>

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
                m_workerThreads.emplace_back(std::thread(&ThreadPool::doWork, this));
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

    /* typename... - "пакет" типов. Их может быть от 0 до "очень много".
     * Для понятности можно провести аналогию с int& - это ссылка на тип int.*/
    template <typename F, typename... Args>
    void Submit(F&& f, Args&&... args)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvProducer.wait(lock, [this] { return m_isDone || m_taskQueue.size() < m_queueSize; });

        if(m_isDone) return;

        // Кстати, начиная с 20 плюсов все вот это делается в одну строчку:
        // auto l = [f = std::forward<F>(f), ...args = std::forward<Args>(args)] mutable{ std::invoke(std::move(f), std::move(args)...); }
        // Просто в 17 стандарте внутрь лямбды ...args мы вот так запросто передать не можем :(
        auto task = [
            
                 /* Создаем внутри лямбды поля func и args.
                 * Нет смысла писать decay_t для F, т.к тип для func выведется автоматически при создании func внутри лямбды.
                 * А вот для элементов кортежа decay_t обязателен. Если пользователь передаст SomeObj& - то в кортеж запакуется ссылка,
                 * которая может умереть до того, как таск выполнится.*/
                func = std::forward<F>(f),
                args = std::tuple<std::decay_t<Args>...>(
                    
                     /* Казалось бы, раз уж мы с помощью decay_t отбрасываем и &, и &&, и const, то что за категорию значения мы пытаемся
                     * сохранить с помощью std::forward? Допустим, пользователь передал в качестве аргумента SomeObj&:
                     * args = std::tuple<SomeObj>(SomeObj&) - вызов для SomeObj конструктора копирования. Пользователь передал SomeObj&& -
                     * args = std::tuple<SomeObj>(SomeObj&&) - вызов конструктора перемещения для SomeObj.
                     * Т.е используя std::forward мы сохраняем намерение пользователя. Если он передал владение на SomeObj - будет перемещение,
                     * а если нет - копирование. Без std::forward в любом случае было бы копирование. А если тип запрещает копирование? Вот и думайте. */
                    std::forward<Args>(args)...
                )
        ]() mutable // mutable нужен потому, что по умолчанию оператор () внутри лямбды имеет модификатор const.
        {
            std::apply(
                [&func](auto&&... unpacked)
                {
                    std::invoke(
                        
                        // Наш таск (внешняя лямбда) уже хранит и переданную функцию, и ее аргументы внутри себя как поля класса.
                        // Внешней лямбде эти поля не будут нужны после выполнения таски - можем спокойно переместить.
                        std::move(func),
                        std::move(unpacked)...
                    );
                },
                // std::apply распакует наш кортеж-tuple в список аргументов и передаст их в свою лямбду.
                std::move(args)
            );
        };

        m_taskQueue.push(std::move(task));

        lock.unlock();

        m_cvConsumer.notify_one();
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