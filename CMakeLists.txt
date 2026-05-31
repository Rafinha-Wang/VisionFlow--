cmake_minimum_required(VERSION 3.16)

project(VisionFlow LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(VisionFlow
    src/main.cpp

    src/app/AppTypes.cpp
    src/app/AutoModeSelector.cpp
    src/app/BatchRunner.cpp
    src/app/PipelineRunner.cpp
    src/app/TuiController.cpp
    src/app/VideoIOAdapter.cpp

    src/core/Image.cpp
    src/core/GrayImage.cpp
    src/core/Timer.cpp

    src/io/BmpReader.cpp
    src/io/BmpWriter.cpp
    src/io/PathUtils.cpp
    src/io/FrameSequence.cpp
    src/io/Vfvid.cpp

    src/cv/GrayConvert.cpp
    src/cv/Sobel.cpp
    src/cv/ImageNormalize.cpp

    src/motion/MotionAnalyzer.cpp
    src/affine/AffineStabilizer.cpp
    src/report/HtmlReport.cpp
)

target_include_directories(VisionFlow PRIVATE src)

if (MSVC)
    target_compile_options(VisionFlow PRIVATE /W4 /permissive-)
else()
    target_compile_options(VisionFlow PRIVATE -Wall -Wextra -pedantic)
endif()
