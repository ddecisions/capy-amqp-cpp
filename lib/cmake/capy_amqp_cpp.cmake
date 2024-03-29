add_library(${PROJECT_LIB} STATIC IMPORTED)

find_library(${PROJECT_LIB}_LIBRARY_PATH ${PROJECT_LIB} HINTS "${CMAKE_CURRENT_LIST_DIR}/../../")
set_target_properties(${PROJECT_LIB} PROPERTIES IMPORTED_LOCATION "${capy_amqp_cpp_LIBRARY_PATH}")

include_directories(
        "${capy_amqp_cpp_INCLUDE_PATH}"
)

message(STATUS "CMAKE_CURRENT_LIST_DIR"  ${CMAKE_CURRENT_LIST_DIR})
message(STATUS "CMAKE_INSTALL_PREFIX " ${CMAKE_INSTALL_PREFIX})
message(STATUS "capy_amqp_cpp_LIBRARY_PATH " ${capy_amqp_cpp_LIB_LIBRARY_PATH})
message(STATUS "capy_amqp_cpp_INCLUDE_PATH " ${capy_amqp_cpp_LIB_INCLUDE_PATH})
