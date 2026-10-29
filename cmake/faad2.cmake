include(FetchContent)
set(FAAD_VERSION "2.11.4")
set(FAAD_ARCHIVE_SHA256 "f4d3630ffdbf17ef17e358007c91d6b51e487de0771dd2d3b3e994d4f0ad81ff")

# A release source archive carries the exact upstream decoder sources here.
# The Git repository keeps only notices and this reproducible build adaptation.
if(NOT FETCHCONTENT_SOURCE_DIR_TTPLAYER_FAAD2 AND
   EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/source/properties.json")
  set(FETCHCONTENT_SOURCE_DIR_TTPLAYER_FAAD2
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/faad2/source")
endif()
FetchContent_Declare(ttplayer_faad2
  URL "https://codeload.github.com/knik0/faad2/zip/refs/tags/${FAAD_VERSION}"
  URL_HASH "SHA256=${FAAD_ARCHIVE_SHA256}"
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  # Populate only; do not build upstream's CLI or its four library variants.
  SOURCE_SUBDIR libfaad)
FetchContent_MakeAvailable(ttplayer_faad2)
file(READ "${ttplayer_faad2_SOURCE_DIR}/properties.json" faad_properties)
string(JSON faad_source_version GET "${faad_properties}" PACKAGE_VERSION)
if(NOT faad_source_version STREQUAL FAAD_VERSION)
  message(FATAL_ERROR "Expected FAAD2 ${FAAD_VERSION}, got ${faad_source_version}")
endif()

# Copy into this build tree before adapting. Never edit a shared download cache.
set(FAAD_PATCHED_DIR "${CMAKE_CURRENT_BINARY_DIR}/faad2-${FAAD_VERSION}-patched")
file(COPY "${ttplayer_faad2_SOURCE_DIR}/libfaad" "${ttplayer_faad2_SOURCE_DIR}/include"
  DESTINATION "${FAAD_PATCHED_DIR}"
  PATTERN "common.h" EXCLUDE PATTERN "neaacdec.h" EXCLUDE)

function(faad_replace file before after)
  file(READ "${ttplayer_faad2_SOURCE_DIR}/${file}" content)
  string(FIND "${content}" "${before}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "FAAD2 ${FAAD_VERSION} adaptation no longer matches ${file}")
  endif()
  string(REPLACE "${before}" "${after}" content "${content}")
  set(output "${FAAD_PATCHED_DIR}/${file}")
  if(EXISTS "${output}")
    file(READ "${output}" previous)
    if(previous STREQUAL content)
      return()
    endif()
  endif()
  file(WRITE "${output}" "${content}")
endfunction()

faad_replace("include/neaacdec.h"
  "  #define NEAACDECAPI __declspec(dllexport)"
  "  /* TTPlayer Rebuild modification, 2026-09-22: static embedding. */\n  #if defined(FAAD_STATIC)\n    #define NEAACDECAPI\n  #else\n    #define NEAACDECAPI __declspec(dllexport)\n  #endif")
faad_replace("libfaad/common.h"
  "  #if defined(_WIN32) && defined(_M_IX86) && !defined(__MINGW32__)\n    #ifndef HAVE_LRINTF\n    #define HAS_LRINTF\n    static INLINE long lrintf(float f)"
  "  /* TTPlayer Rebuild modification, 2026-09-22: keep x87 rounding,\n     with CRT declarations before the private MSVC helper alias. */\n  #include <math.h>\n  #if defined(_WIN32) && defined(_M_IX86) && !defined(__MINGW32__)\n    #ifndef HAVE_LRINTF\n    #define HAS_LRINTF\n    #define lrintf faad_lrintf\n    static INLINE long faad_lrintf(float f)")

include("${CMAKE_CURRENT_LIST_DIR}/faad2_compat.cmake")

file(GLOB FAAD_SOURCES CONFIGURE_DEPENDS "${FAAD_PATCHED_DIR}/libfaad/*.c")
add_library(ttp_faad STATIC ${FAAD_SOURCES})
target_include_directories(ttp_faad PUBLIC "${FAAD_PATCHED_DIR}/include" PRIVATE "${FAAD_PATCHED_DIR}/libfaad")
target_compile_definitions(ttp_faad PRIVATE _CRT_SECURE_NO_WARNINGS
  PACKAGE_VERSION="${FAAD_VERSION}" APPLY_DRC=1)
target_compile_definitions(ttp_faad PUBLIC FAAD_STATIC FAAD2_VERSION="${FAAD_VERSION}")
# Keep floating-point output and avoid contraction/reassociation. Upstream fixes
# change some PCM samples compared with the original FAAD2 2.7 decoder.
target_compile_options(ttp_faad PRIVATE /Os /Gy /Gw /GF /fp:precise /arch:SSE2 /utf-8 /wd4244 /wd4267 /wd4996)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/faad2-source-path.txt" "${ttplayer_faad2_SOURCE_DIR}")
