# Copyright Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

# Pinned snapshot of AMD-DCTOOLS/addc-base 9a9675f (3.3.0), built as a static
# library and linked into libamd_smi. amdsmi includes only addc/lib_c.h (a plain C
# header), so the C++20 sources never reach amdsmi's C++17 translation units.
# On a version bump, re-copy the snapshot and update the list below to match the
# upstream meson.build; the drift check fails the configure if they disagree.

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 10)
    message(
        FATAL_ERROR
        "addc-base needs a C++20 compiler (GCC 10 or newer); found GCC ${CMAKE_CXX_COMPILER_VERSION}"
    )
endif()

# A system nlohmann_json is preferred. Otherwise use the vendored 3.12.0 release
# (include.zip, sha256 b8cb0ef2dd7f57f18933997c9934bb1fa962594f701cd5a8d3c2c80541559372),
# so a build needs no extra package.
find_package(nlohmann_json 3.11 QUIET)
if(nlohmann_json_FOUND)
    message(STATUS "addc-base: using system nlohmann_json ${nlohmann_json_VERSION}")
else()
    message(STATUS "addc-base: using vendored nlohmann_json 3.12.0")
    add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
    set_target_properties(
        nlohmann_json::nlohmann_json
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/third_party/nlohmann_json/include"
    )
endif()

set(ADDC_PROJECTS FIRERANGE GENOA MI300A MI300C MI300X MI325X MI350 MI450 TURIN VENICE)
set(ADDC_VERSION "3.3.0")

set(ADDC_SOURCES
    src/addc-common/base64.cpp
    src/addc-cper-parser/cper-parse.cpp
    src/addc-cper-parser/cper-record.cpp
    src/addc-cper-parser/section_dispatch.cpp
    src/addc-cper-parser/sections/cper-section-ainic.cpp
    src/addc-cper-parser/sections/cper-section-amd-epyc-cdd.cpp
    src/addc-cper-parser/sections/cper-section-amd-epyc-crashdump.cpp
    src/addc-cper-parser/sections/cper-section-amd-epyc-mtl.cpp
    src/addc-cper-parser/sections/cper-section-amd-epyc-pmic.cpp
    src/addc-cper-parser/sections/cper-section-amd-mi300-crashdump.cpp
    src/addc-cper-parser/sections/cper-section-amd-mi300-runtime.cpp
    src/addc-cper-parser/sections/cper-section-amd-mi450-crashdump.cpp
    src/addc-cper-parser/sections/cper-section-amd-mi450-mtl.cpp
    src/addc-cper-parser/sections/cper-section-ia32x64.cpp
    src/addc-cper-parser/sections/cper-section-pcie.cpp
    src/addc-cper-parser/structural.cpp
    src/addc-cper-pipeline/detect.cpp
    src/addc-cper-pipeline/pipeline.cpp
    src/addc-cper-pipeline/section_dispatch.cpp
    src/addc-cper-pipeline/sections/ainic_decoder.cpp
    src/addc-cper-pipeline/sections/ainic_section.cpp
    src/addc-cper-pipeline/sections/cdd_decoder.cpp
    src/addc-cper-pipeline/sections/cdd_section.cpp
    src/addc-cper-pipeline/sections/epyc_crashdump_decoder.cpp
    src/addc-cper-pipeline/sections/epyc_crashdump_section.cpp
    src/addc-cper-pipeline/sections/gpu_crashdump_decoder.cpp
    src/addc-cper-pipeline/sections/gpu_crashdump_section.cpp
    src/addc-cper-pipeline/sections/gpu_mi450_crashdump_decoder.cpp
    src/addc-cper-pipeline/sections/gpu_mi450_crashdump_section.cpp
    src/addc-cper-pipeline/sections/gpu_mi450_runtime_decoder.cpp
    src/addc-cper-pipeline/sections/gpu_mi450_runtime_section.cpp
    src/addc-cper-pipeline/sections/gpu_runtime_decoder.cpp
    src/addc-cper-pipeline/sections/gpu_runtime_section.cpp
    src/addc-cper-pipeline/sections/ia32x64_decoder.cpp
    src/addc-cper-pipeline/sections/ia32x64_section.cpp
    src/addc-cper-pipeline/sections/mi450_mtl_section.cpp
    src/addc-cper-pipeline/sections/mtl_decoder.cpp
    src/addc-cper-pipeline/sections/mtl_section.cpp
    src/addc-cper-pipeline/sections/pcie_decoder.cpp
    src/addc-cper-pipeline/sections/pcie_section.cpp
    src/addc-cper-pipeline/sections/pmic_decoder.cpp
    src/addc-cper-pipeline/sections/pmic_section.cpp
    src/addc-cper-pipeline/sections/section_conversion.cpp
    src/addc-decoder/boot/boot_afid.cpp
    src/addc-decoder/boot_mi450/decoder.cpp
    src/addc-decoder/dbglog/decoder.cpp
    src/addc-decoder/dbglog/mi450.cpp
    src/addc-decoder/dbglog/venice.cpp
    src/addc-decoder/outbound/msg.cpp
    src/addc-decoder/pcie/decoder.cpp
    src/addc-decoder/wdt/decoder.cpp
    src/addc-decoder/wdt/venice.cpp
    src/addc-mca/decoder.cpp
    src/addc-mca/mi300a.cpp
    src/addc-mca/mi300x.cpp
    src/addc-mca/mi450.cpp
    src/addc-mca/policy.cpp
    src/addc-mca/venice/venice.cpp
    src/api/lib_c.cpp
    src/api/lib.cpp
    src/core/base_report.cpp
    src/core/runtime.cpp
    src/products/product.cpp
)

file(GLOB_RECURSE _addc_found RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "src/*.cpp")
set(_addc_listed ${ADDC_SOURCES})
list(SORT _addc_found)
list(SORT _addc_listed)
if(NOT "${_addc_found}" STREQUAL "${_addc_listed}")
    message(
        FATAL_ERROR
        "third_party/addc-base source list is out of date: the .cpp files under src/ "
        "differ from ADDC_SOURCES in ${CMAKE_CURRENT_LIST_FILE}"
    )
endif()

list(TRANSFORM ADDC_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_library(addc_base STATIC ${ADDC_SOURCES})
set_target_properties(
    addc_base
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

target_include_directories(
    addc_base
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/src/tier_api"
)

target_compile_definitions(addc_base PRIVATE ADDC_BUILDING_LIB "ADDC_VERSION=\"${ADDC_VERSION}\"")
foreach(_project IN LISTS ADDC_PROJECTS)
    target_compile_definitions(addc_base PRIVATE ADDC_HAS_${_project})
endforeach()

# Vendored code: keep the parent's warning flags out, and undo its -fno-rtti so
# the sources build as upstream does (RTTI on).
target_compile_options(addc_base PRIVATE -w -frtti)

# nlohmann_json is header-only; BUILD_INTERFACE keeps it out of the exported
# amd_smi link interface.
target_link_libraries(addc_base PRIVATE "$<BUILD_INTERFACE:nlohmann_json::nlohmann_json>")
