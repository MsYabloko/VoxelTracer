cmake_minimum_required(VERSION 3.24)
project(VoxelTracer)

set(CMAKE_CXX_STANDARD 17)

file(GLOB_RECURSE IMGUI_FILES external/imgui/*.cpp)

add_executable(VoxelTracer src/main.cpp src/utils/Texture.h src/utils/Texture.cpp ${IMGUI_FILES} external/stb_image/stb_image.cpp )

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(external/glfwSources)

target_link_libraries(VoxelTracer glfw)

find_package(OpenGL REQUIRED)

target_link_libraries(VoxelTracer OpenGL::GL)

add_subdirectory(external/glad)
target_link_libraries(VoxelTracer glad)

add_subdirectory(external/glm)
target_link_libraries(VoxelTracer glm)

target_include_directories(VoxelTracer PRIVATE external/imgui)
target_include_directories(VoxelTracer PRIVATE external/stb_image)

file(COPY data DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")