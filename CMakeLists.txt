cmake_minimum_required(VERSION 3.0)

project(Scene-Renderer)
include(CTest)

file(GLOB_RECURSE SRC_LIST ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE IMGUI_LIST ${PROJECT_SOURCE_DIR}/external/imgui/*.cpp ${PROJECT_SOURCE_DIR}/external/imgui/*.h)
file(GLOB_RECURSE DDS_LIST ${PROJECT_SOURCE_DIR}/external/libdds/*.cpp)
file(GLOB JSON_LIST ${PROJECT_SOURCE_DIR}/external/json/json.hpp)
set(GLAD_DIR ${PROJECT_SOURCE_DIR}/external/glad/glad.c)
file(GLOB_RECURSE HEADER_LIST ${PROJECT_SOURCE_DIR}/include/*.h)

add_executable(${PROJECT_NAME} ${SRC_LIST} ${HEADER_LIST} ${GLAD_DIR} ${IMGUI_LIST}
    ${DDS_LIST}
    ${JSON_LIST})


target_include_directories(${PROJECT_NAME} 
    PUBLIC ${PROJECT_SOURCE_DIR}/include 
    PUBLIC ${PROJECT_SOURCE_DIR}/external 
    )

set(LIBRARAY ${PROJECT_SOURCE_DIR}/lib)

target_link_libraries(${PROJECT_NAME} 
    PUBLIC ${LIBRARAY}/libglfw3.lib 
    PUBLIC ${LIBRARAY}/libassimpd.lib
    PUBLIC ${LIBRARAY}/libyaml-cppd.lib) 

target_link_libraries(${PROJECT_NAME} PUBLIC "-lopengl32") 

add_test(NAME run COMMAND ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.exe WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
# add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND cp ${LIBRARAY}/*.dll ${PROJECT_BINARY_DIR}/)