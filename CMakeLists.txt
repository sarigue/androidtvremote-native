cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(
    androidtvremote
    VERSION 0.2.0
    DESCRIPTION "Native C++ Android TV Remote v2 client library"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CTest)

option(ANDROIDTVREMOTE_BUILD_EXAMPLES "Build command-line examples" ON)
option(ANDROIDTVREMOTE_BUILD_TESTS "Build unit tests" ${BUILD_TESTING})

function(androidtvremote_enable_test_asserts target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
endfunction()

find_package(OpenSSL 1.1.1 REQUIRED)
find_package(Boost 1.74 REQUIRED)
find_package(Threads REQUIRED)

add_library(androidtvremote
    src/android_tv_remote.cpp
    src/certificate.cpp
    src/pairing.cpp
    src/protocol_messages.cpp
    src/proto_wire.cpp)
add_library(androidtvremote::androidtvremote ALIAS androidtvremote)

target_compile_features(androidtvremote PUBLIC cxx_std_20)
target_include_directories(androidtvremote
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${Boost_INCLUDE_DIRS})

target_compile_definitions(androidtvremote PRIVATE
    BOOST_ERROR_CODE_HEADER_ONLY
    BOOST_SYSTEM_NO_DEPRECATED)

target_link_libraries(androidtvremote
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(androidtvremote
        PUBLIC ANDROIDTVREMOTE_SHARED
        PRIVATE ANDROIDTVREMOTE_BUILDING_LIBRARY)
    set_target_properties(androidtvremote PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES)
endif()

if(WIN32)
    target_link_libraries(androidtvremote PRIVATE ws2_32)
endif()

set_target_properties(androidtvremote PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME androidtvremote)

if(MSVC)
    target_compile_options(androidtvremote PRIVATE /W4 /permissive-)
else()
    target_compile_options(androidtvremote PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ANDROIDTVREMOTE_BUILD_EXAMPLES)
    add_executable(androidtvremote-cli examples/remote_cli.cpp)
    target_link_libraries(androidtvremote-cli PRIVATE androidtvremote::androidtvremote)
endif()

if(ANDROIDTVREMOTE_BUILD_TESTS)
    add_executable(androidtvremote-tests
        tests/test_protocol.cpp
        src/protocol_messages.cpp
        src/proto_wire.cpp)
    target_compile_features(androidtvremote-tests PRIVATE cxx_std_20)
    androidtvremote_enable_test_asserts(androidtvremote-tests)
    target_include_directories(androidtvremote-tests PRIVATE include src)
    add_test(NAME androidtvremote-tests COMMAND androidtvremote-tests)

    add_executable(androidtvremote-cert-test tests/test_certificate.cpp)
    androidtvremote_enable_test_asserts(androidtvremote-cert-test)
    target_link_libraries(androidtvremote-cert-test PRIVATE androidtvremote::androidtvremote)
    add_test(NAME androidtvremote-cert-test COMMAND androidtvremote-cert-test)

    add_executable(androidtvremote-integration-test tests/test_remote_integration.cpp)
    androidtvremote_enable_test_asserts(androidtvremote-integration-test)
    target_include_directories(androidtvremote-integration-test PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(androidtvremote-integration-test PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
    target_link_libraries(androidtvremote-integration-test PRIVATE
        androidtvremote::androidtvremote OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    add_test(NAME androidtvremote-integration-test COMMAND androidtvremote-integration-test)

    add_executable(androidtvremote-pairing-test tests/test_pairing_integration.cpp)
    androidtvremote_enable_test_asserts(androidtvremote-pairing-test)
    target_include_directories(androidtvremote-pairing-test PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(androidtvremote-pairing-test PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
    target_link_libraries(androidtvremote-pairing-test PRIVATE
        androidtvremote::androidtvremote OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    add_test(NAME androidtvremote-pairing-test COMMAND androidtvremote-pairing-test)
endif()

install(TARGETS androidtvremote
    EXPORT androidtvremoteTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/androidtvremoteConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    cmake/androidtvremoteConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/androidtvremoteConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/androidtvremote)

install(EXPORT androidtvremoteTargets
    FILE androidtvremoteTargets.cmake
    NAMESPACE androidtvremote::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/androidtvremote)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/androidtvremoteConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/androidtvremoteConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/androidtvremote)
