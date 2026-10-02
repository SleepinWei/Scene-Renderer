cmake_minimum_required(VERSION 3.16)

project(Scene-Renderer LANGUAGES C CXX)
include(CTest)
option(SCENERENDERER_METAL "Use native Metal on macOS" ${APPLE})
if(SCENERENDERER_METAL AND NOT APPLE)
    message(FATAL_ERROR "Metal requires macOS")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(APPLE)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(HOMEBREW_PREFIX)
        list(PREPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
        list(PREPEND CMAKE_INCLUDE_PATH "${HOMEBREW_PREFIX}/include")
    endif()
endif()

file(GLOB_RECURSE SRC_LIST CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB IMGUI_LIST CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/external/imgui/*.cpp)
file(GLOB DDS_LIST CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/external/libdds/*.cpp)
if(SCENERENDERER_METAL)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    list(FILTER SRC_LIST EXCLUDE REGEX "/utils/Shader\\.cpp$")
    list(FILTER IMGUI_LIST EXCLUDE REGEX "imgui_impl_opengl3\\.cpp$")
    list(APPEND SRC_LIST "${PROJECT_SOURCE_DIR}/src/metal/MetalBackend.mm")
else()
    list(FILTER SRC_LIST EXCLUDE REGEX "/metal/")
endif()
set(GLAD_SRC ${PROJECT_SOURCE_DIR}/external/glad/glad.c)

add_executable(${PROJECT_NAME}
    ${SRC_LIST}
    ${GLAD_SRC}
    ${IMGUI_LIST}
    ${DDS_LIST}
)

# Vendored headers (glad, glm, imgui, stb, json, tinygltf).
# On macOS, Homebrew include is placed first so glfw / assimp / yaml-cpp
# match the dylibs we link, instead of the Windows-era copies under external/.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external
)

target_compile_definitions(${PROJECT_NAME} PRIVATE GLFW_INCLUDE_NONE)

if(WIN32)
    set(LIBRARY ${PROJECT_SOURCE_DIR}/lib)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${LIBRARY}/glfw3.lib
        ${LIBRARY}/assimp-vc143-mtd.lib
        ${LIBRARY}/yaml-cppd.lib
        opengl32
    )
else()
    if(NOT SCENERENDERER_METAL)
        find_package(OpenGL REQUIRED)
    endif()
    find_package(glfw3 CONFIG REQUIRED)
    find_package(assimp CONFIG REQUIRED)
    find_package(yaml-cpp CONFIG REQUIRED)

    if(HOMEBREW_PREFIX)
        target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
            ${HOMEBREW_PREFIX}/include
        )
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE
        glfw
        assimp::assimp
        yaml-cpp::yaml-cpp
    )

    if(SCENERENDERER_METAL)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        find_program(GLSLANG_EXECUTABLE glslangValidator REQUIRED)
        find_program(SPIRV_CROSS_EXECUTABLE spirv-cross REQUIRED)
        set(METAL_SHADER_DIR "${CMAKE_BINARY_DIR}/metal/shaders")
        file(GLOB_RECURSE EFFECT_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/shader/*")
        add_custom_command(OUTPUT "${METAL_SHADER_DIR}/complete.stamp"
            COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/compile_metal_shaders.py" --output "${METAL_SHADER_DIR}"
            COMMAND ${CMAKE_COMMAND} -E touch "${METAL_SHADER_DIR}/complete.stamp"
            DEPENDS ${EFFECT_SOURCES} "${PROJECT_SOURCE_DIR}/tools/compile_metal_shaders.py"
            VERBATIM)
        add_custom_target(metal-shaders DEPENDS "${METAL_SHADER_DIR}/complete.stamp")
        add_dependencies(${PROJECT_NAME} metal-shaders)
        target_compile_definitions(${PROJECT_NAME} PRIVATE SCENERENDERER_METAL SR_METAL_SHADER_DIR="${METAL_SHADER_DIR}")
        set_source_files_properties(src/metal/MetalBackend.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Metal" "-framework Cocoa" "-framework QuartzCore")
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
    endif()
    if(APPLE)
        target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)
    endif()
endif()

if(SCENERENDERER_METAL)
    add_test(NAME metal-gpu COMMAND ${PROJECT_NAME} --metal-self-test)
    add_test(NAME metal-classic-gallery COMMAND ${PROJECT_NAME} --render-gallery "${CMAKE_BINARY_DIR}/gallery")
    set_tests_properties(metal-gpu metal-classic-gallery PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
