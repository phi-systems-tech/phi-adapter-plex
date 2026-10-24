cmake_minimum_required(VERSION 3.21)

project(phi-adapter-plex VERSION 1.0.0 DESCRIPTION "Phi Plex adapter" LANGUAGES CXX)

include(GNUInstallDirs)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json 3.11 REQUIRED)

set(PHI_ADAPTER_SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../phi-adapter-sdk" CACHE PATH
    "Path to local phi-adapter-sdk checkout")
option(PHI_ADAPTER_PLEX_USE_LOCAL_ADAPTER_SDK "Use local ../phi-adapter-sdk" ON)

if(NOT TARGET phi::adapter-sdk)
    if(PHI_ADAPTER_PLEX_USE_LOCAL_ADAPTER_SDK AND EXISTS "${PHI_ADAPTER_SDK_SOURCE_DIR}/CMakeLists.txt")
        set(PHI_ADAPTER_SDK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${PHI_ADAPTER_SDK_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/phi-adapter-sdk")
    else()
        find_package(phi-adapter-sdk CONFIG REQUIRED)
    endif()
endif()

add_library(phi_adapter_plex_core STATIC
    src/plex_instance.cpp
    src/plex_json.cpp
    src/plex_model.cpp
    src/plex_protocol.cpp
    src/plex_schema.cpp
    src/plex_settings.cpp
)
target_compile_features(phi_adapter_plex_core PUBLIC cxx_std_20)
set_target_properties(phi_adapter_plex_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(phi_adapter_plex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(phi_adapter_plex_core PUBLIC phi::adapter-sdk nlohmann_json::nlohmann_json)

add_executable(phi_adapter_plex_ipc src/main.cpp)
target_link_libraries(phi_adapter_plex_ipc PRIVATE phi_adapter_plex_core)
set_target_properties(phi_adapter_plex_ipc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/adapters")

enable_testing()
add_executable(plex_behaviour_tests tests/plex_behaviour_tests.cpp)
target_link_libraries(plex_behaviour_tests PRIVATE phi_adapter_plex_core)
add_test(NAME plex_behaviour_tests COMMAND plex_behaviour_tests)
set_tests_properties(plex_behaviour_tests PROPERTIES TIMEOUT 120)

install(TARGETS phi_adapter_plex_ipc
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/phi/plugins/adapters)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/plex-config.json"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/phi/plugins/adapters)
