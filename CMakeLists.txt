cmake_minimum_required(VERSION 3.22)
project(rocos_comm VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(fastdds 3.6.2.0 EXACT REQUIRED)
find_package(fastcdr 2.3.6 EXACT REQUIRED)
find_package(Threads REQUIRED)
include(cmake/RocosGenerateIdl.cmake)

rocos_generate_idl(rocos_wire FILES interfaces/ServiceFrame.idl)
add_library(rocos_comm comm/src/node.cpp)
add_library(rocos_comm::rocos_comm ALIAS rocos_comm)
target_compile_features(rocos_comm PUBLIC cxx_std_17)
target_compile_options(rocos_comm PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(rocos_comm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/comm/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(rocos_comm PUBLIC fastdds fastcdr Threads::Threads rocos_wire)

install(TARGETS rocos_comm rocos_wire EXPORT rocos_commTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY comm/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY ${rocos_wire_GENERATED_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rocos_wire
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.ipp")
install(EXPORT rocos_commTargets NAMESPACE rocos_comm:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rocos_comm)
configure_package_config_file(cmake/rocos_commConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rocos_commConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rocos_comm)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/rocos_commConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rocos_commConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/rocos_commConfigVersion.cmake cmake/RocosGenerateIdl.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rocos_comm)

option(ROCOS_BUILD_EXAMPLES "Build service and pubsub examples" ON)
include(CTest)
if(ROCOS_BUILD_EXAMPLES OR BUILD_TESTING)
    rocos_generate_idl(demo_types FILES examples/robot/Robot.idl)
endif()
if(ROCOS_BUILD_EXAMPLES)
    add_executable(rocos_demo examples/robot/main.cpp)
    target_link_libraries(rocos_demo PRIVATE rocos_comm demo_types)
endif()
if(BUILD_TESTING)
    add_executable(comm_tests tests/comm_tests.cpp)
    target_link_libraries(comm_tests PRIVATE rocos_comm demo_types)
    add_test(NAME comm_integration COMMAND comm_tests)
    set_tests_properties(comm_integration PROPERTIES TIMEOUT 120)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(ROCOS_DEPENDENCY_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/work/deps-install" CACHE PATH "Dependency prefix for independent acceptance test")
    add_test(NAME external_acceptance COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/external_acceptance.py
        --build ${CMAKE_CURRENT_BINARY_DIR} --dependencies ${ROCOS_DEPENDENCY_PREFIX}
        --generator ${FASTDDSGEN_EXECUTABLE})
    set_tests_properties(external_acceptance PROPERTIES TIMEOUT 120)
    if(ROCOS_BUILD_EXAMPLES)
        add_test(NAME process_integration COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/process_integration.py $<TARGET_FILE:rocos_demo>)
        set_tests_properties(process_integration PROPERTIES TIMEOUT 90)
    endif()
endif()
