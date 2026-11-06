cmake_minimum_required(VERSION 3.24)
project(ShaprLinux VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets Test)
find_package(OpenCASCADE REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)

set(SHAPR_WARNINGS -Wall -Wextra -Wno-unused-parameter)

# Geometry/model code with no Qt or GUI dependency, so it can be unit tested.
add_library(shapr_core STATIC
  src/model/Export.cpp
  src/model/Export.h
  src/model/Features.cpp
  src/model/Features.h
  src/model/Profiles.cpp
  src/model/Profiles.h
  src/model/Solids.cpp
  src/model/Solids.h
  src/sketch/Constraint.h
  src/sketch/ConstraintCommands.cpp
  src/sketch/ConstraintCommands.h
  src/sketch/DrawList.cpp
  src/sketch/DrawList.h
  src/sketch/Geometry.cpp
  src/sketch/Geometry.h
  src/sketch/Offset.cpp
  src/sketch/Offset.h
  src/sketch/Sketch.cpp
  src/sketch/Sketch.h
  src/sketch/Snapper.cpp
  src/sketch/Snapper.h
  src/sketch/Solver.cpp
  src/sketch/Solver.h
  src/sketch/Trim.cpp
  src/sketch/Trim.h
)
target_include_directories(shapr_core PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
target_compile_options(shapr_core PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(shapr_core PUBLIC
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKShHealing TKFillet
  TKMesh TKXSBase TKDESTEP TKDESTL
  Eigen3::Eigen)

# Project files (Qt Core for JSON), shared by the app and its tests.
add_library(shapr_io STATIC src/io/ProjectFile.cpp src/io/ProjectFile.h)
target_compile_options(shapr_io PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(shapr_io PUBLIC shapr_core Qt6::Core)

add_executable(shapr_linux
  src/main.cpp
  src/app/MainWindow.cpp
  src/app/Icons.cpp
  src/app/Icons.h
  src/app/MainWindow.h
  src/app/Theme.cpp
  src/app/Theme.h
  src/sketch/SketchEditor.cpp
  src/sketch/SketchEditorBodies.cpp
  src/sketch/SketchEditor.h
  src/sketch/SketchTools.cpp
  src/sketch/SketchTools.h
  src/viewer/Glyphs.cpp
  src/viewer/Glyphs.h
  src/viewer/OcctViewerWidget.cpp
  src/viewer/OcctViewerWidget.h
  src/viewer/PenSample.h
  src/viewer/PlanarPrs.cpp
  src/viewer/PlanarPrs.h
  src/viewer/ValueEditor.cpp
  src/viewer/ValueEditor.h
  src/viewer/ViewerInteraction.h
)
target_compile_definitions(shapr_linux PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_compile_options(shapr_linux PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(shapr_linux PRIVATE
  shapr_core
  shapr_io
  Qt6::Widgets
  Qt6::OpenGLWidgets
  TKG3d TKBRep TKGeomBase TKTopAlgo TKPrim TKBO TKFillet
  TKService TKV3d TKOpenGl
)

enable_testing()
foreach(name sketch solver trim offset model features)
  add_executable(test_${name} tests/test_${name}.cpp tests/Check.h)
  target_compile_options(test_${name} PRIVATE ${SHAPR_WARNINGS})
  target_link_libraries(test_${name} PRIVATE shapr_core)
  add_test(NAME ${name} COMMAND test_${name})
endforeach()

add_executable(test_project tests/test_project.cpp tests/Check.h)
target_compile_options(test_project PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(test_project PRIVATE shapr_io)
add_test(NAME project COMMAND test_project)

# The floating value field, on Qt's offscreen platform (no display needed).
add_executable(test_value_editor tests/test_value_editor.cpp src/viewer/ValueEditor.cpp src/viewer/ValueEditor.h)
target_include_directories(test_value_editor PRIVATE src)
target_compile_options(test_value_editor PRIVATE ${SHAPR_WARNINGS})
target_link_libraries(test_value_editor PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME value_editor COMMAND test_value_editor)
set_tests_properties(value_editor PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
