cmake_minimum_required(VERSION 3.28)
project(mu_core_py LANGUAGES CXX)

find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)
include(FetchContent)


FetchContent_Declare(
        nanobind
        GIT_REPOSITORY https://github.com/wjakob/nanobind.git
        GIT_TAG        v2.12.0
)
FetchContent_MakeAvailable(nanobind)


nanobind_add_module(_mu_core src/bind.cpp)

target_link_libraries(_mu_core PRIVATE mu_core)

install(TARGETS _mu_core LIBRARY DESTINATION .)


