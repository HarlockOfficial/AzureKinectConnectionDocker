cmake_minimum_required(VERSION 3.5)
project(k4a_native_server_modular)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(K4A REQUIRED libk4a)
pkg_check_modules(K4ABT REQUIRED libk4abt)
pkg_check_modules(ZeroMQ REQUIRED libzmq)
find_package(Threads REQUIRED)

include_directories(${K4A_INCLUDE_DIRS} ${K4ABT_INCLUDE_DIRS} /usr/include)
link_directories(${K4A_LIBRARY_DIRS} ${K4ABT_LIBRARY_DIRS})

add_executable(k4a_server
  src/main.cpp
  src/K4ADevice.cpp
  src/BodyTracker.cpp
  src/ZmqPublisher.cpp
  src/CheckKinect.cpp
)

target_link_libraries(k4a_server
  ${K4A_LIBRARIES}
  ${K4ABT_LIBRARIES}
  ${ZeroMQ_LIBRARIES}
  Threads::Threads
)
