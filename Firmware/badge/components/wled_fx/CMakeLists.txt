# SPDX-License-Identifier: EUPL-1.2
# wled_fx: WLED v16.0.1 effects + palettes as one 1D segment. upstream/ and gen/ are produced by
# tools/wled_port/wledport.py import and must not be edited; adaptation lives in shim/ and src/.
# Keep the source list and defines in sync with tools/wled_port/host/Makefile.
set(up upstream/wled00)
set(upstream_srcs
    ${up}/FX.cpp ${up}/palettes.cpp ${up}/wled_math.cpp
    ${up}/src/dependencies/fastled_slim/fastled_slim.cpp
    gen/carved_FX_fcn.cpp gen/carved_util.cpp gen/colors.cpp)
# community/*.cpp effects brought in by `wledport.py add`; the list is generated next to them (and being
# include()d, a change to it re-runs CMake by itself).
include(${CMAKE_CURRENT_LIST_DIR}/community/community.cmake)

idf_component_register(
    SRCS ${upstream_srcs} ${wled_fx_community_srcs} shim/wled_fx_rt.cpp src/wled_fx_api.cpp
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS shim gen community ${up}
    PRIV_REQUIRES esp_hw_support freertos)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    WLED_DISABLE_2D WLED_DISABLE_PARTICLESYSTEM2D WLED_DISABLE_PARTICLESYSTEM1D
    WLED_PS_DONT_REPLACE_FX BOARD_HAS_PSRAM)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)

# Upstream WLED code is compiled as-is; silence its warnings there only.
set_source_files_properties(${upstream_srcs} ${wled_fx_community_srcs} PROPERTIES COMPILE_OPTIONS "-w")
