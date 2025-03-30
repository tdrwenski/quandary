cmake_minimum_required(VERSION 3.14)

project(Quandary 
  VERSION 1.0.0
  LANGUAGES CXX C
  DESCRIPTION "Optimal control for open and closed quantum systems"
)

# Enable MPI support in BLT
set(ENABLE_MPI ON CACHE BOOL "")

# Include BLT
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/blt/SetupBLT.cmake)
  message(FATAL_ERROR "The BLT submodule is not present. Run: git submodule update --init")
endif()
set(ENABLE_GTEST OFF CACHE BOOL "")
set(ENABLE_GMOCK OFF CACHE BOOL "")
set(ENABLE_BENCHMARKS OFF CACHE BOOL "")
include(blt/SetupBLT.cmake)

# Define options
option(WITH_SLEPC "Enable SLEPc for eigenvalue problems" OFF)
option(SANITY_CHECK "Enable sanity checks" OFF)

# Allow specifying custom PETSc location
set(PETSC_DIR "" CACHE PATH "Path to PETSc installation")

# Find MPI (required by PETSc)
# This is already handled by BLT when ENABLE_MPI=ON

# Find PETSc
if(PETSC_DIR)
  # Use custom location if provided
  set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${PETSC_DIR}/lib/pkgconfig")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(PETSC REQUIRED IMPORTED_TARGET PETSc)

# Find SLEPc if requested
if(WITH_SLEPC)
  set(SLEPC_DIR "" CACHE PATH "Path to SLEPc installation")
  if(SLEPC_DIR)
    set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${SLEPC_DIR}/lib/pkgconfig")
  endif()
  pkg_check_modules(SLEPC REQUIRED IMPORTED_TARGET SLEPc)
endif()

# Collect source files
file(GLOB SOURCES 
  "src/*.cpp"
  "src/*/*.cpp"
)

# Add executable with BLT
blt_add_executable(
  NAME quandary
  SOURCES ${SOURCES}
  DEPENDS_ON mpi
)

# Add include directories
target_include_directories(quandary PRIVATE 
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link with PETSc and conditionally with SLEPc
target_link_libraries(quandary PRIVATE 
  PkgConfig::PETSC
  $<$<BOOL:${WITH_SLEPC}>:PkgConfig::SLEPC>
)

# Add compiler definitions
target_compile_definitions(quandary PRIVATE
  $<$<BOOL:${WITH_SLEPC}>:WITH_SLEPC>
  $<$<BOOL:${SANITY_CHECK}>:SANITY_CHECK>
)

# Set C++ standard
target_compile_features(quandary PRIVATE cxx_std_14)

# Add optimization flags
target_compile_options(quandary PRIVATE -O3)

# Installation
install(TARGETS quandary
  RUNTIME DESTINATION bin
)

# Install config template
install(FILES config_template.cfg
  DESTINATION share/quandary
)

# Install python interface
install(FILES quandary.py
  DESTINATION lib/python
)