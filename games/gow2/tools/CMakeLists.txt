# God of War II vertente tools. Enabled with -DPS2X_BUILD_GOW2_TOOLS=ON.
add_executable(gow2_gs_replay gs_replay.cpp)
target_include_directories(gow2_gs_replay PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
target_link_libraries(gow2_gs_replay PRIVATE ps2_runtime)

# Standalone OpenGL/EGL microbenchmark (no runtime dependency).
find_library(GOW2_EGL_LIB EGL)
find_library(GOW2_GL_LIB GL)
if(GOW2_EGL_LIB AND GOW2_GL_LIB)
    add_executable(gow2_gpu_bench gpu_bench.cpp)
    target_link_libraries(gow2_gpu_bench PRIVATE ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()
