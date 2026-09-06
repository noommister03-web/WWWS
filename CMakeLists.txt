cmake_minimum_required(VERSION 3.20)

project(telegram_crm VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(tg_bot
    main.cpp
    Config.cpp
    Database.cpp
    TelegramBot.cpp
    AiEngine.cpp
    CustoJustoClient.cpp
)

target_include_directories(tg_bot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(tg_bot PRIVATE
    CURL::libcurl
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(tg_bot PRIVATE /W4)
else()
    target_compile_options(tg_bot PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
