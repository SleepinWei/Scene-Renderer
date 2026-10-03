cmake_minimum_required(VERSION 3.18)

project(Scene-Renderer LANGUAGES C CXX)
include(CTest)
option(SCENERENDERER_GI_TESTS "Test the separately downloaded large GI scenes" OFF)
option(SCENERENDERER_VULKAN_PROTOTYPE "Build standalone Vulkan RHI offscreen validation" OFF)
option(SCENERENDERER_LEGACY_METAL "Build retired Metal GL compatibility renderer and gallery" OFF)
option(SCENERENDERER_METAL "Use native Metal on macOS" ${APPLE})
set(SCENERENDERER_RHI_BACKEND "AUTO" CACHE STRING "RHI backend: AUTO, Metal, Vulkan, OpenGL")
set_property(CACHE SCENERENDERER_RHI_BACKEND PROPERTY STRINGS AUTO Metal OpenGL Vulkan)
if(SCENERENDERER_RHI_BACKEND STREQUAL "Metal")
    set(SCENERENDERER_METAL ON)
elseif(SCENERENDERER_RHI_BACKEND STREQUAL "OpenGL")
    set(SCENERENDERER_METAL OFF)
elseif(SCENERENDERER_RHI_BACKEND STREQUAL "Vulkan")
    set(SCENERENDERER_METAL OFF)
    set(SCENERENDERER_VULKAN_PROTOTYPE ON)
    set(SCENERENDERER_DEFAULT_VULKAN ON)
elseif(NOT SCENERENDERER_RHI_BACKEND STREQUAL "AUTO")
    message(FATAL_ERROR "Unknown SCENERENDERER_RHI_BACKEND: ${SCENERENDERER_RHI_BACKEND}")
endif()
if(SCENERENDERER_METAL AND NOT APPLE)
    message(FATAL_ERROR "Metal requires macOS")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set_source_files_properties(src/engine/RenderRuntime.cpp PROPERTIES LANGUAGE OBJCXX COMPILE_FLAGS "-fobjc-arc")
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
list(FILTER SRC_LIST EXCLUDE REGEX "/rhi/VulkanDevice\\.cpp$")
if(SCENERENDERER_METAL)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    if(SCENERENDERER_LEGACY_METAL)
        list(FILTER SRC_LIST EXCLUDE REGEX "/utils/Shader\\.cpp$")
    else()
        list(FILTER SRC_LIST EXCLUDE REGEX "/metal/")
    endif()
    list(FILTER SRC_LIST EXCLUDE REGEX "/rhi/OpenGLDevice\\.cpp$")
    list(FILTER IMGUI_LIST EXCLUDE REGEX "imgui_impl_opengl3\\.cpp$")
    list(APPEND SRC_LIST "${PROJECT_SOURCE_DIR}/src/rhi/MetalDevice.mm")
    if(SCENERENDERER_LEGACY_METAL)
        list(APPEND SRC_LIST "${PROJECT_SOURCE_DIR}/src/metal/MetalBackend.mm")
    endif()
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
if(SCENERENDERER_DEFAULT_VULKAN)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SCENERENDERER_DEFAULT_VULKAN)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(GLSLANG_EXECUTABLE glslangValidator REQUIRED)
find_program(SPIRV_CROSS_EXECUTABLE spirv-cross REQUIRED)
set(RHI_SHADER_DIR "${CMAKE_BINARY_DIR}/rhi/shaders")
set(RHI_SHADER_FLAGS)
if(SCENERENDERER_METAL)
    list(APPEND RHI_SHADER_FLAGS --metal)
endif()
file(GLOB RHI_SHADER_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/rhi/shaders/*")
add_custom_command(OUTPUT "${RHI_SHADER_DIR}/complete.stamp"
    COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/compile_rhi_shaders.py" --output "${RHI_SHADER_DIR}" ${RHI_SHADER_FLAGS}
    COMMAND ${CMAKE_COMMAND} -E touch "${RHI_SHADER_DIR}/complete.stamp"
    DEPENDS ${RHI_SHADER_SOURCES} "${PROJECT_SOURCE_DIR}/tools/compile_rhi_shaders.py" VERBATIM)
add_custom_target(rhi-shaders DEPENDS "${RHI_SHADER_DIR}/complete.stamp")
add_dependencies(${PROJECT_NAME} rhi-shaders)
target_compile_definitions(${PROJECT_NAME} PRIVATE SR_RHI_SHADER_DIR="${RHI_SHADER_DIR}")

if(WIN32 AND NOT SCENERENDERER_DEFAULT_VULKAN AND NOT SCENERENDERER_VULKAN_PROTOTYPE)
    set(LIBRARY ${PROJECT_SOURCE_DIR}/lib)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${LIBRARY}/glfw3.lib
        ${LIBRARY}/assimp-vc143-mtd.lib
        ${LIBRARY}/yaml-cppd.lib
        opengl32
    )
else()
    if(NOT SCENERENDERER_METAL AND NOT SCENERENDERER_DEFAULT_VULKAN)
        find_package(OpenGL REQUIRED)
    endif()
    if(SCENERENDERER_VULKAN_PROTOTYPE)
        find_package(glfw3 3.4 CONFIG REQUIRED)
    else()
        find_package(glfw3 CONFIG REQUIRED)
    endif()
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
        if(SCENERENDERER_LEGACY_METAL)
        set(METAL_SHADER_DIR "${CMAKE_BINARY_DIR}/metal/shaders")
        file(GLOB_RECURSE EFFECT_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/shader/*")
        add_custom_command(OUTPUT "${METAL_SHADER_DIR}/complete.stamp"
            COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/compile_metal_shaders.py" --output "${METAL_SHADER_DIR}"
            COMMAND ${CMAKE_COMMAND} -E touch "${METAL_SHADER_DIR}/complete.stamp"
            DEPENDS ${EFFECT_SOURCES} "${PROJECT_SOURCE_DIR}/tools/compile_metal_shaders.py"
            VERBATIM)
        add_custom_target(metal-shaders DEPENDS "${METAL_SHADER_DIR}/complete.stamp")
        add_dependencies(${PROJECT_NAME} metal-shaders)
        target_compile_definitions(${PROJECT_NAME} PRIVATE SCENERENDERER_LEGACY_METAL SR_METAL_SHADER_DIR="${METAL_SHADER_DIR}")
        endif()
        target_compile_definitions(${PROJECT_NAME} PRIVATE SCENERENDERER_METAL)
        set_source_files_properties(src/metal/MetalBackend.mm src/rhi/MetalDevice.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Metal" "-framework Cocoa" "-framework QuartzCore")
    elseif(NOT SCENERENDERER_DEFAULT_VULKAN)
        target_link_libraries(${PROJECT_NAME} PRIVATE OpenGL::GL)
    endif()
    if(APPLE)
        target_compile_definitions(${PROJECT_NAME} PRIVATE GL_SILENCE_DEPRECATION)
        target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Foundation")
    endif()
endif()

if(SCENERENDERER_METAL AND SCENERENDERER_LEGACY_METAL)
    add_test(NAME metal-gpu COMMAND ${PROJECT_NAME} --metal-self-test)
    add_test(NAME metal-classic-gallery COMMAND ${PROJECT_NAME} --legacy-gallery "${CMAKE_BINARY_DIR}/gallery" core)
    set_tests_properties(metal-gpu metal-classic-gallery PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    if(SCENERENDERER_GI_TESTS)
        add_test(NAME metal-gi-gallery COMMAND ${PROJECT_NAME} --legacy-gallery "${CMAKE_BINARY_DIR}/gi-gallery" gi)
        set_tests_properties(metal-gi-gallery PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    endif()
endif()
if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    add_executable(pt-cpu-tests tests/PT/CpuPathTracerTests.cpp src/PT/CpuPathTracer.cpp)
    target_include_directories(pt-cpu-tests PRIVATE include external)
    target_link_libraries(pt-cpu-tests PRIVATE Threads::Threads)
    add_test(NAME pt-cpu COMMAND pt-cpu-tests)
    set_tests_properties(pt-cpu PROPERTIES TIMEOUT 60)
    add_test(NAME pt-cornell-scene COMMAND ${PROJECT_NAME} --path-trace cornell --pt-no-sky --pt-size 64x64 --pt-samples 4 --pt-bounces 4 --pt-output "${CMAKE_BINARY_DIR}/pt/cornell")
    set_tests_properties(pt-cornell-scene PROPERTIES TIMEOUT 60 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    if(SCENERENDERER_METAL OR SCENERENDERER_DEFAULT_VULKAN)
        add_test(NAME pt-native-sky COMMAND ${PROJECT_NAME} --pt-self-test)
        set_tests_properties(pt-native-sky PROPERTIES LABELS "gpu" TIMEOUT 60 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    endif()
    add_executable(engine-concurrency-tests tests/engine/ConcurrencyTests.cpp src/engine/JobSystem.cpp)
    target_include_directories(engine-concurrency-tests PRIVATE include)
    target_link_libraries(engine-concurrency-tests PRIVATE Threads::Threads)
    add_test(NAME engine-concurrency COMMAND engine-concurrency-tests)
    set_tests_properties(engine-concurrency PROPERTIES TIMEOUT 20)
    add_executable(rhi-image-decoder-tests tests/rhi/ImageDecoderTests.cpp src/renderer/rhi/DdsDecoder.cpp)
    target_include_directories(rhi-image-decoder-tests PRIVATE include external)
    add_test(NAME rhi-image-decoder COMMAND rhi-image-decoder-tests)
    add_executable(rhi-contract-tests tests/rhi/DeviceTests.cpp src/rhi/Device.cpp src/rhi/Validation.cpp)
    target_include_directories(rhi-contract-tests PRIVATE "${PROJECT_SOURCE_DIR}/include")
    add_test(NAME rhi-contract COMMAND rhi-contract-tests)
    add_executable(rhi-graphics-contract-tests tests/rhi/GraphicsTests.cpp src/rhi/Device.cpp src/rhi/GraphicsDevice.cpp)
    target_include_directories(rhi-graphics-contract-tests PRIVATE include)
    add_test(NAME rhi-graphics-contract COMMAND rhi-graphics-contract-tests)
    add_test(NAME rhi-forward-scene COMMAND ${PROJECT_NAME} --rhi-forward --hidden --frames 3 --screenshot "${CMAKE_BINARY_DIR}/rhi/forward-scene.ppm")
    set_tests_properties(rhi-forward-scene PROPERTIES LABELS "gpu" TIMEOUT 60 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME rhi-deferred-scene COMMAND ${PROJECT_NAME} --rhi-deferred --hidden --frames 3 --screenshot "${CMAKE_BINARY_DIR}/rhi/deferred-scene.ppm")
    set_tests_properties(rhi-deferred-scene PROPERTIES LABELS "gpu" TIMEOUT 60 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME rhi-gpu COMMAND ${PROJECT_NAME} --rhi-self-test)
    set_tests_properties(rhi-gpu PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} LABELS "gpu" TIMEOUT 60)
    if(SCENERENDERER_METAL OR SCENERENDERER_DEFAULT_VULKAN)
        add_test(NAME rhi-editor-resize COMMAND ${PROJECT_NAME} --demo --hidden --size 800x450 --frames 20 --time 8 --resize 640x360 --screenshot "${CMAKE_BINARY_DIR}/rhi/editor-resize.ppm")
        add_test(NAME rhi-editor-terrain COMMAND ${PROJECT_NAME} --classic terrain --hidden --size 640x360 --frames 40 --time 8 --resize 480x270 --screenshot "${CMAKE_BINARY_DIR}/rhi/editor-terrain.ppm")
        add_test(NAME rhi-editor-single-thread COMMAND ${PROJECT_NAME} --classic cornell --single-thread --hidden --size 640x360 --frames 8 --time 8)
        add_test(NAME rhi-editor-forward COMMAND ${PROJECT_NAME} --classic cornell --forward --hidden --size 640x360 --frames 8 --time 8 --screenshot "${CMAKE_BINARY_DIR}/rhi/editor-forward.ppm")
        set_tests_properties(rhi-editor-resize rhi-editor-forward rhi-editor-terrain rhi-editor-single-thread PROPERTIES LABELS "gpu" TIMEOUT 90 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    endif()
    if(NOT SCENERENDERER_METAL AND NOT SCENERENDERER_DEFAULT_VULKAN)
        add_executable(rhi-opengl-state-tests tests/rhi/OpenGLStateValidation.cpp
            src/rhi/Device.cpp src/rhi/GraphicsDevice.cpp src/rhi/ShaderAssets.cpp
            src/rhi/GraphicsValidation.cpp src/rhi/ComputeValidation.cpp src/rhi/OpenGLDevice.cpp ${GLAD_SRC})
        target_include_directories(rhi-opengl-state-tests PRIVATE include external)
        target_compile_definitions(rhi-opengl-state-tests PRIVATE GLFW_INCLUDE_NONE SR_RHI_SHADER_DIR="${RHI_SHADER_DIR}")
        add_dependencies(rhi-opengl-state-tests rhi-shaders)
        if(WIN32 AND NOT SCENERENDERER_VULKAN_PROTOTYPE)
            target_link_libraries(rhi-opengl-state-tests PRIVATE ${LIBRARY}/glfw3.lib opengl32)
        else()
            target_link_libraries(rhi-opengl-state-tests PRIVATE glfw OpenGL::GL)
        endif()
        add_test(NAME rhi-opengl-state COMMAND rhi-opengl-state-tests)
        set_tests_properties(rhi-opengl-state PROPERTIES LABELS "gpu" TIMEOUT 60 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    endif()
endif()

if(SCENERENDERER_VULKAN_PROTOTYPE)
    find_package(Vulkan REQUIRED)
    # Prefer the SDK GLFW 3.4 headers over the retired vendored 3.3 headers.
    target_include_directories(${PROJECT_NAME} BEFORE PRIVATE "$<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>")
    target_sources(${PROJECT_NAME} PRIVATE src/rhi/VulkanDevice.cpp)
    target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan)
    target_compile_definitions(${PROJECT_NAME} PRIVATE SCENERENDERER_HAS_VULKAN)
    add_executable(rhi-vulkan-validation tests/rhi/VulkanValidation.cpp src/engine/JobSystem.cpp
        src/rhi/Device.cpp src/rhi/GraphicsDevice.cpp src/rhi/ShaderAssets.cpp
        src/rhi/Validation.cpp src/rhi/FrameValidation.cpp src/rhi/GraphicsValidation.cpp src/rhi/ComputeValidation.cpp src/rhi/VulkanDevice.cpp
        src/renderer/rhi/GpuMesh.cpp src/renderer/rhi/GpuMaterial.cpp src/renderer/rhi/GpuImageCache.cpp src/renderer/rhi/ImageDecoder.cpp src/renderer/rhi/DdsDecoder.cpp
        src/renderer/rhi/ForwardPbrRenderer.cpp src/renderer/rhi/ForwardValidation.cpp
        src/renderer/rhi/GpuOcean.cpp src/renderer/rhi/OceanValidation.cpp src/renderer/rhi/ShadowRenderer.cpp src/renderer/rhi/SceneEffectsValidation.cpp
        src/renderer/rhi/GpuAtmosphere.cpp src/renderer/rhi/OceanSurface.cpp src/renderer/rhi/AtmosphereValidation.cpp src/renderer/rhi/GpuSubdivision.cpp src/renderer/rhi/SubdivisionValidation.cpp src/renderer/rhi/GpuTerrain.cpp src/renderer/rhi/GpuVirtualTexture.cpp src/renderer/rhi/VirtualTextureValidation.cpp src/renderer/rhi/TerrainValidation.cpp src/renderer/rhi/GpuGrass.cpp src/renderer/rhi/GpuTemporal.cpp src/renderer/rhi/TemporalValidation.cpp)
    target_include_directories(rhi-vulkan-validation BEFORE PRIVATE "$<TARGET_PROPERTY:glfw,INTERFACE_INCLUDE_DIRECTORIES>")
    target_include_directories(rhi-vulkan-validation PRIVATE include external)
    target_link_libraries(rhi-vulkan-validation PRIVATE Vulkan::Vulkan glfw)
    set_property(TARGET ${PROJECT_NAME} rhi-vulkan-validation PROPERTY NO_SYSTEM_FROM_IMPORTED TRUE)
    if(HOMEBREW_PREFIX)
        target_include_directories(rhi-vulkan-validation BEFORE PRIVATE ${HOMEBREW_PREFIX}/include)
    endif()
    target_compile_definitions(rhi-vulkan-validation PRIVATE SR_RHI_SHADER_DIR="${RHI_SHADER_DIR}")
    add_dependencies(rhi-vulkan-validation rhi-shaders)
    if(BUILD_TESTING)
        add_test(NAME rhi-vulkan-gpu COMMAND rhi-vulkan-validation)
        set_tests_properties(rhi-vulkan-gpu PROPERTIES LABELS "gpu" TIMEOUT 60 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        if(APPLE)
            # MetalTools instrumentation stalls MoltenVK on this host; use Vulkan validation when available.
            set_tests_properties(rhi-vulkan-gpu PROPERTIES ENVIRONMENT "MTL_DEBUG_LAYER=0;MTL_SHADER_VALIDATION=0")
        endif()
    endif()
endif()
