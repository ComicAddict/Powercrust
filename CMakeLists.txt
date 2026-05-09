cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)

project(SURFACERECONSTRUCTION VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
add_library(PowerCrust powercrust.cxx)
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

# ── reconstruct executable ────────────────────────────────────────────────────
add_executable(reconstruct ReconstructFromPoints.cxx)
target_compile_features(reconstruct PRIVATE cxx_std_17)
target_link_libraries(reconstruct PRIVATE PowerCrust)

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS reconstruct
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS PowerCrust
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(FILES powercrust.h MeshIO.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/powercrust)
