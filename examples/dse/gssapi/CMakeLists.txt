cmake_minimum_required(VERSION 3.10)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ".")

# Set up rules/commands for building gssapi example
set(PROJECT_EXAMPLE_NAME gssapi)
set(EXAMPLE_SRC_FILES gssapi.c)
include_directories(${INCLUDES})
add_executable(${PROJECT_EXAMPLE_NAME} ${EXAMPLE_SRC_FILES})
target_link_libraries(${PROJECT_EXAMPLE_NAME} ${PROJECT_LIB_NAME_TARGET} ${DSE_LIBS})
add_dependencies(${PROJECT_EXAMPLE_NAME} ${PROJECT_LIB_NAME_TARGET})

set_target_properties(${PROJECT_EXAMPLE_NAME} PROPERTIES FOLDER "Examples"
                                                         COMPILE_FLAGS "${EXAMPLE_CMAKE_C_FLAGS}")

# Set up rules/commands for building gssapi_proxy example
set(PROJECT_EXAMPLE_NAME gssapi_proxy)
set(EXAMPLE_SRC_FILES gssapi_proxy.c)
add_executable(${PROJECT_EXAMPLE_NAME} ${EXAMPLE_SRC_FILES})
target_link_libraries(${PROJECT_EXAMPLE_NAME} ${PROJECT_LIB_NAME_TARGET} ${DSE_LIBS})
add_dependencies(${PROJECT_EXAMPLE_NAME} ${PROJECT_LIB_NAME_TARGET})

set_target_properties(${PROJECT_EXAMPLE_NAME} PROPERTIES FOLDER "Examples"
                                                         COMPILE_FLAGS "${EXAMPLE_CMAKE_C_FLAGS}")
