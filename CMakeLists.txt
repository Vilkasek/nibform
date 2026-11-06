cmake_minimum_required(VERSION 3.24)
project(ShaprLinux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(OpenCASCADE REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)

add_executable(shapr_linux
  src/main.cpp
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/viewer/OcctViewerWidget.cpp
  src/viewer/OcctViewerWidget.h
  src/viewer/PenSample.h
)

target_include_directories(shapr_linux PRIVATE src ${OpenCASCADE_INCLUDE_DIR})
target_compile_definitions(shapr_linux PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_compile_options(shapr_linux PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(shapr_linux PRIVATE
  Qt6::Widgets
  Qt6::OpenGLWidgets
  Eigen3::Eigen
  TKernel TKMath TKG3d TKBRep TKGeomBase TKTopAlgo TKPrim TKBO TKFillet
  TKService TKV3d TKOpenGl
)
