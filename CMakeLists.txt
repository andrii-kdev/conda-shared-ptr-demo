cmake_minimum_required(VERSION 3.10)

project(shared-ptr-demo VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost REQUIRED CONFIG COMPONENTS system filesystem atomic)

add_executable(shared-ptr-demo source/shared-ptr-demo.cpp)

target_compile_options(shared-ptr-demo PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(shared-ptr-demo PRIVATE 
    Boost::system 
    Boost::filesystem 
    Boost::atomic)

include(GNUInstallDirs)
install(TARGETS shared-ptr-demo DESTINATION ${CMAKE_INSTALL_BINDIR})
