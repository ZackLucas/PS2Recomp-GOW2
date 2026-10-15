# God of War II vertente tools. Enabled with -DPS2X_BUILD_GOW2_TOOLS=ON.
add_executable(gow2_gs_replay gs_replay.cpp)
target_include_directories(gow2_gs_replay PRIVATE ${CMAKE_SOURCE_DIR}/ps2xRuntime/include)
target_link_libraries(gow2_gs_replay PRIVATE ps2_runtime)
