# CMakeList.txt: проект CMake для async-http-server; включите исходный код и определения,
# укажите здесь логику для конкретного проекта.
#
cmake_minimum_required (VERSION 3.10)

# Включение горячей перезагрузки для компиляторов MSVC, если поддерживается.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("async-http-server")

# Добавьте источник в исполняемый файл этого проекта.
add_executable(server
    main.cpp
    src/connection.cpp
    src/connection_map.cpp
    src/reactor.cpp
    src/server.cpp
    src/thread_pool.cpp
)

# Заголовочные файлы
set(HEADERS
    include/server.hpp
    include/connection.hpp
    include/connection_map.hpp
    include/reactor.hpp
    include/thread_pool.hpp
)

# Заголовки
target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)


if(UNIX)
    target_compile_options(server PRIVATE
        -Wall
        -Wextra
        -pthread
    )
    target_link_libraries(server PRIVATE pthread)
endif()


