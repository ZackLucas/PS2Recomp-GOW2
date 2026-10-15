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

# GLSL VRAM addressing vs the runtime's C++ implementation (needs ps2_runtime for GSMem).
if(GOW2_EGL_LIB AND GOW2_GL_LIB)
    add_executable(gow2_gs_addr_test gs_addr_test.cpp)
    target_include_directories(gow2_gs_addr_test PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
    target_compile_definitions(gow2_gs_addr_test PRIVATE GOW2_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    target_link_libraries(gow2_gs_addr_test PRIVATE ps2_runtime ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()

# GPU image upload vs GSCpuBackend::UploadImage.
if(GOW2_EGL_LIB AND GOW2_GL_LIB)
    add_executable(gow2_gs_upload_test gs_upload_test.cpp)
    target_include_directories(gow2_gs_upload_test PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
    target_compile_definitions(gow2_gs_upload_test PRIVATE GOW2_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    target_link_libraries(gow2_gs_upload_test PRIVATE ps2_runtime ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()

# GPU CLUT load + palette lookup vs GSCpuBackend.
if(GOW2_EGL_LIB AND GOW2_GL_LIB)
    add_executable(gow2_gs_clut_test gs_clut_test.cpp)
    target_include_directories(gow2_gs_clut_test PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
    target_compile_definitions(gow2_gs_clut_test PRIVATE GOW2_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    target_link_libraries(gow2_gs_clut_test PRIVATE ps2_runtime ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()

# GPU per-pixel pipeline vs GSCpuBackend::WritePixel.
if(GOW2_EGL_LIB AND GOW2_GL_LIB)
    add_executable(gow2_gs_pixel_test gs_pixel_test.cpp)
    target_include_directories(gow2_gs_pixel_test PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
    target_compile_definitions(gow2_gs_pixel_test PRIVATE GOW2_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    target_link_libraries(gow2_gs_pixel_test PRIVATE ps2_runtime ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()

# GPU primitive rendering (coverage, interpolation, texturing, per-pixel pipeline) vs GSCpuBackend.
if(GOW2_EGL_LIB AND GOW2_GL_LIB)
    add_executable(gow2_gs_draw_test gs_draw_test.cpp)
    target_include_directories(gow2_gs_draw_test PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
    target_compile_definitions(gow2_gs_draw_test PRIVATE GOW2_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    target_link_libraries(gow2_gs_draw_test PRIVATE ps2_runtime ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()
