if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.env)
    file (WRITE ".env" "CAPY_AMQP_ADDRESS=amqp://guest:guest@localhost:5672/")
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.env)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/.env ${CMAKE_CURRENT_BINARY_DIR}/.env COPYONLY)
endif()

include_directories(../../src/broker_impl)

set (TEST api-async-rpc-server-test)
include(../CMakeCommonTest.in)
