cmake_minimum_required(VERSION 3.24)
project(ShaprLinux VERSION 0.2.0 LANGUAGES CXX)

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

set(SHAPR_WARNINGS -Wall -Wextra -Wno-unused-parameter)

# Geometry/model code with no Qt or GUI dependency, so it can be unit tested.
add_library(shapr_core STATIC
  src/sketch/DrawList.cpp
  src/sketch/DrawList.h
  src/sketch/Sketch.cpp
  src/sketch/Sketch.h
  src/sketch/Snapper.cpp
  src/sketch/Snapper.h
)
target_include_directories(shapr_core PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
target_compile_options(shapr_core PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(shapr_core PUBLIC TKernel TKMath Eigen3::Eigen)

add_executable(shapr_linux
  src/main.cpp
  src/app/MainWindow.cpp
  src/app/MainWindow.h
  src/sketch/SketchEditor.cpp
  src/sketch/SketchEditor.h
  src/sketch/SketchTools.cpp
  src/sketch/SketchTools.h
  src/viewer/OcctViewerWidget.cpp
  src/viewer/OcctViewerWidget.h
  src/viewer/PenSample.h
  src/viewer/PlanarPrs.cpp
  src/viewer/PlanarPrs.h
  src/viewer/ViewerInteraction.h
)
target_compile_definitions(shapr_linux PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_compile_options(shapr_linux PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(shapr_linux PRIVATE
  shapr_core
  Qt6::Widgets
  Qt6::OpenGLWidgets
  TKG3d TKBRep TKGeomBase TKTopAlgo TKPrim TKBO TKFillet
  TKService TKV3d TKOpenGl
)

enable_testing()
add_executable(test_sketch tests/test_sketch.cpp)
target_compile_options(test_sketch PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(test_sketch PRIVATE shapr_core)
add_test(NAME sketch COMMAND test_sketch)
