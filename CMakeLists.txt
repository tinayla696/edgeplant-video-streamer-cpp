cmake_minimum_required(VERSION 3.10)
project(edgeplant-video-streamer-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(GSTREAMER REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
)

add_executable(${PROJECT_NAME}
    src/app/main.cpp
    src/streaming/pipeline_factory.cpp
    src/streaming/streamer.cpp
    src/http/http_server.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GSTREAMER_INCLUDE_DIRS}
    src
)

link_directories(${GSTREAMER_LIBRARY_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${GSTREAMER_LIBRARIES} Threads::Threads)
target_compile_options(${PROJECT_NAME} PRIVATE ${GSTREAMER_CFLAGS_OTHER})
