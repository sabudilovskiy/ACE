cmake_minimum_required(VERSION 3.27)
include(CreatePart.cmake)
include(FetchContent)
set(CMAKE_CXX_STANDARD 20)
project(ACE)

find_package(fmt CONFIG REQUIRED)
CreateTrivialPart(
        name "engine"
        path "/src/engine"
)

CreateTrivialExe(
        name "main"
        path "/src/app"
)
target_link_libraries(
    ${main_exe} PRIVATE ${engine_module}
)
target_link_libraries(${main_exe} PRIVATE fmt::fmt)