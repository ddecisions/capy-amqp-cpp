include(CMakePackageConfigHelpers)

set(CMAKE_INSTALL_PREFIX /usr/local/${PROJECT_LIB})

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/
        ${CMAKE_CURRENT_SOURCE_DIR}/src/
        ${EXTERNAL_LOCATION}
        ${OPENSSL_INCLUDE_DIR}
        ${libuv_INCLUDEDIR}
)

FILE(GLOB PUBLIC_INCLUDE_FILES
        ${CMAKE_SOURCE_DIR}/include/capy/*.h
        ${EXTERNAL_LOCATION}/dotenv/*.h
        )

FILE(GLOB PUBLIC_NLOHMANN_INCLUDE_FILES
        ${EXTERNAL_LOCATION}/nlohmann/*.h
        )

FILE(GLOB PUBLIC_DOTENV_INCLUDE_FILES
        ${EXTERNAL_LOCATION}/dotenv/*.h
        )

FILE(GLOB INCLUDE_FILES
        ${PUBLIC_INCLUDE_FILES}
        )

FILE(GLOB SOURCES
        ../src/*.cc
        ../src/broker_impl/*.cc
        ../src/deferred_mpl/*.cc
        )

add_library(
        ${PROJECT_LIB}
        ${SOURCES})

target_link_libraries (
        ${PROJECT_LIB}  PUBLIC
        ${Boost_LIBRARIES}
)

target_include_directories(
        ${PROJECT_LIB}
        PUBLIC
        ../include
        ${EXTERNAL_INCLUDE_DIR}
        ${EXTERNAL_LOCATION}/nlohmann
)

set(config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Configuration
set(version_config "${generated_dir}/${PROJECT_NAME}ConfigVersion.cmake")
set(project_config "${generated_dir}/${PROJECT_NAME}Config.cmake")
set(TARGETS_EXPORT_NAME "${PROJECT_NAME}")
set(namespace "${PROJECT_NAME}::")

write_basic_package_version_file(
        "${version_config}" COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
        "cmake/Config.cmake.in"
        "${project_config}"
        INSTALL_DESTINATION "${config_install_dir}"
)

install(TARGETS ${PROJECT_LIB}   DESTINATION lib)
install(FILES   ${PUBLIC_INCLUDE_FILES} DESTINATION include/capy)
install(FILES   ${PUBLIC_NLOHMANN_INCLUDE_FILES} DESTINATION include/nlohmann)
install(FILES   ${PUBLIC_DOTENV_INCLUDE_FILES} DESTINATION include/dotenv)
install(FILES   ${project_config} ${version_config} DESTINATION lib/cmake/${PROJECT_LIB})
install(FILES   cmake/${PROJECT_LIB}.cmake DESTINATION lib/cmake/${PROJECT_LIB})