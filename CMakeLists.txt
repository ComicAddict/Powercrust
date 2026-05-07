cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)

project(SURFACERECONSTRUCTION VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── VTK ──────────────────────────────────────────────────────────────────────
find_package(VTK REQUIRED)
if(VTK_VERSION VERSION_LESS "8.90.0")
  include(${VTK_USE_FILE})
endif()

# ── OpenMP ────────────────────────────────────────────────────────────────────
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  message(STATUS "OpenMP ${OpenMP_CXX_VERSION} found – parallelisation enabled")
else()
  message(STATUS "OpenMP not found – building single-threaded")
endif()

# ── AVX2 / FMA ───────────────────────────────────────────────────────────────
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
check_cxx_compiler_flag("-mfma"  COMPILER_SUPPORTS_FMA)

if(MSVC)
  check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_AVX2)
endif()

if(COMPILER_SUPPORTS_AVX2)
  message(STATUS "AVX2 support enabled")
else()
  message(STATUS "AVX2 not supported by this compiler – scalar fallbacks will be used")
endif()

# ── Directories ───────────────────────────────────────────────────────────────
include_directories(${SURFACERECONSTRUCTION_SOURCE_DIR})

# ── PowerCrust library ────────────────────────────────────────────────────────
add_library(PowerCrust vtkPowerCrustSurfaceReconstruction.cxx)
target_compile_features(PowerCrust PUBLIC cxx_std_17)

if(OpenMP_CXX_FOUND)
  target_link_libraries(PowerCrust PUBLIC OpenMP::OpenMP_CXX)
  target_compile_definitions(PowerCrust PUBLIC PC_USE_OPENMP)
endif()

if(COMPILER_SUPPORTS_AVX2)
  if(MSVC)
    target_compile_options(PowerCrust PRIVATE /arch:AVX2)
  else()
    target_compile_options(PowerCrust PRIVATE -mavx2)
    if(COMPILER_SUPPORTS_FMA)
      target_compile_options(PowerCrust PRIVATE -mfma)
    endif()
  endif()
  target_compile_definitions(PowerCrust PUBLIC PC_USE_AVX2)
endif()

if(VTK_VERSION VERSION_LESS "8.90.0")
  target_link_libraries(PowerCrust PUBLIC ${VTK_LIBRARIES})
else()
  target_link_libraries(PowerCrust PUBLIC ${VTK_LIBRARIES})
endif()

# ── reconstruct executable ────────────────────────────────────────────────────
add_executable(reconstruct ReconstructFromPoints.cxx)
target_compile_features(reconstruct PRIVATE cxx_std_17)

if(VTK_VERSION VERSION_LESS "8.90.0")
  target_link_libraries(reconstruct PRIVATE ${VTK_LIBRARIES} PowerCrust)
else()
  target_link_libraries(reconstruct PRIVATE ${VTK_LIBRARIES} PowerCrust)
  vtk_module_autoinit(TARGETS reconstruct MODULES ${VTK_LIBRARIES})
endif()
