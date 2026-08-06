cmake_minimum_required(VERSION 3.20)
project(ThreadPool LANGUAGES CXX)

# 1. Настройки стандарта C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 2. Находим системную библиотеку потоков (pthread для Linux)
find_package(Threads REQUIRED)

# 3. Объявляем нашу header-only библиотеку
add_library(ThreadPool INTERFACE)

# Указываем, где лежат наши заголовочные файлы (.hpp)
target_include_directories(ThreadPool INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# 4. Объявляем исполняемый файл примера (наш main.cpp)
add_executable(PoolSample
        samples/main.cpp
        include/ThreadPool/ThreadPool.hpp)

# Связываем пример с нашей библиотекой и потоками ОС
target_link_libraries(PoolSample PRIVATE ThreadPool Threads::Threads)

# 5. [ОПЦИОНАЛЬНО] Флаги санитайзера для поиска гонок (TSan)
# target_compile_options(pool_example PRIVATE -fsanitize=thread)
# target_link_options(pool_example PRIVATE -fsanitize=thread)
