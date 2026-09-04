cmake_minimum_required(VERSION 3.10)
project(sifonsub)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/Release)
set(GGML_VULKAN ON CACHE BOOL "" FORCE)
add_subdirectory(external/whisper.cpp)
add_executable(sifonsub
    src/main.cpp
    src/audio_converter.cpp
    src/transcriber.cpp
    src/subtitle_writer.cpp
)
target_link_libraries(sifonsub PRIVATE whisper)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)

add_executable(qt_test WIN32
    src/qt_test_main.cpp
    src/audio_converter.cpp
    src/transcriber.cpp
    src/subtitle_writer.cpp
    src/app_icon.rc
)
target_link_libraries(qt_test PRIVATE Qt6::Widgets Qt6::Network whisper)
target_include_directories(qt_test PRIVATE src)

add_custom_command(TARGET qt_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:qt_test>/ffmpeg"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/external/ffmpeg/ffmpeg.exe"
        "$<TARGET_FILE_DIR:qt_test>/ffmpeg/ffmpeg.exe"
)
