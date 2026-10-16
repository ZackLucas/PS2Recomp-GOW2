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

# Replay a PCSX2 GS dump through the frontend with CPU + GPU backends and compare them.
if(GOW2_EGL_LIB AND GOW2_GL_LIB)
    add_executable(gow2_gs_gpu_replay gs_gpu_replay.cpp)
    target_include_directories(gow2_gs_gpu_replay PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
    target_compile_definitions(gow2_gs_gpu_replay PRIVATE GOW2_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    target_link_libraries(gow2_gs_gpu_replay PRIVATE ps2_runtime ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()

# GPU GS backend for the live runtime: compiled into ps2EntryRunner and enabled with PS2_GS_BACKEND=gpu.
if(TARGET ps2EntryRunner AND GOW2_EGL_LIB AND GOW2_GL_LIB)
    target_sources(ps2EntryRunner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gs_gpu_backend.cpp)
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/gs_gpu_backend.cpp TARGET_DIRECTORY ps2EntryRunner PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
        SKIP_UNITY_BUILD_INCLUSION ON
        INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/ps2xRuntime/include;${CMAKE_CURRENT_SOURCE_DIR}"
        COMPILE_DEFINITIONS "GOW2_GLFW_CONTEXT=1;GOW2_SHADER_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\"")
    target_link_libraries(ps2EntryRunner PRIVATE ${GOW2_EGL_LIB} ${GOW2_GL_LIB})
endif()

# Native VU1 upper-unit semantics (ps2_vu1_native.h) vs VU1Interpreter, per instruction.
add_executable(gow2_vu1_native_test vu1_native_test.cpp)
target_include_directories(gow2_vu1_native_test PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
target_link_libraries(gow2_vu1_native_test PRIVATE ps2_runtime)
