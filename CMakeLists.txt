cmake_minimum_required(VERSION 3.10)
project(bms_ai)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)
ExternalProject_Add(piper_phonemize_external
        GIT_REPOSITORY https://github.com/rhasspy/piper-phonemize.git
        GIT_TAG master
        INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/install/piper_phonemize
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel 4)

ExternalProject_Add(sherpa_onnx_external
        GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
        GIT_TAG master
        INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/install/sherpa-onnx
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DBUILD_SHARED_LIBS=ON -DSHERPA_ONNX_ENABLE_C_API=ON
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel 4)

ExternalProject_Add(llama_cpp_external
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp
        GIT_TAG master
        INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/install/llama.cpp
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DGGML_METAL=OFF
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel 4)

ExternalProject_Add(whisper_cpp_external
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG master
        INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/install/whisper.cpp
        CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DGGML_METAL=OFF
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel 4)

ExternalProject_Add(piper_irina_model
        URL "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-ru_RU-irina-medium.tar.bz2"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND ""
        UPDATE_COMMAND ""
        LOG_DOWNLOAD ON)

add_custom_target(dependencies
        DEPENDS piper_phonemize_external sherpa_onnx_external llama_cpp_external whisper_cpp_external piper_irina_model)

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "${CMAKE_CURRENT_LIST_DIR}/install/llama.cpp/bin/llama-server"
        "${CMAKE_BINARY_DIR}/llama-server"
        RESULT_VARIABLE LINK_RESULT)

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "${CMAKE_BINARY_DIR}/install/llama.cpp/bin/llama-server"
        "${CMAKE_BINARY_DIR}/llama-server"
        RESULT_VARIABLE LINK_RESULT)

execute_process(
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
        "${CMAKE_BINARY_DIR}/piper_irina_model-prefix/src/piper_irina_model"
        "${CMAKE_BINARY_DIR}/piper_irina_model"
        RESULT_VARIABLE LINK_RESULT)

find_package(curl REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(bms_ai
        main.cpp
        TTS.cpp)

add_dependencies(bms_ai dependencies)

set(SHERPA_ROOT "${CMAKE_CURRENT_LIST_DIR}/install/sherpa-onnx")
set(PIPER_ROOT "${CMAKE_CURRENT_LIST_DIR}/install/piper_phonemize")

target_include_directories(bms_ai PRIVATE
        ${SHERPA_ROOT}/include
        ${PIPER_ROOT}/include)

set_target_properties(bms_ai PROPERTIES
        BUILD_RPATH "${SHERPA_ROOT}/lib;${PIPER_ROOT}/lib")

target_link_libraries(bms_ai PRIVATE
        curl
        nlohmann_json::nlohmann_json
        -L${SHERPA_ROOT}/lib
        sherpa-onnx-cxx-api
        sherpa-onnx-c-api
        onnxruntime
        -L${PIPER_ROOT}/lib
        piper_phonemize
        espeak-ng)

#if (0)
#set(SHERPA_ROOT "${CMAKE_CURRENT_LIST_DIR}/sherpa-onnx/install")
#set(PIPER_ROOT "${CMAKE_CURRENT_LIST_DIR}/piper-phonemize")
#set(PIPER_ORT ${PIPER_ROOT}/lib/onnxruntime-osx-arm64-1.14.1/lib)
#
#target_include_directories(bms_ai PRIVATE
#        "${SHERPA_ROOT}/include"
#        "${PIPER_ROOT}/src"
#        "${PIPER_ROOT}/build/ei/include"
#        "${PIPER_ROOT}/lib/onnxruntime-osx-arm64-1.14.1/include"
#)
#
#find_library(SHERPA_CXX_LIB
#        NAMES sherpa-onnx-cxx-api
#        PATHS "${SHERPA_ROOT}/lib"
#        REQUIRED)
#
#find_library(SHERPA_C_LIB
#        NAMES sherpa-onnx-c-api
#        PATHS "${SHERPA_ROOT}/lib"
#        REQUIRED)
#
#find_library(ONNXRUNTIME_LIB
#        NAMES onnxruntime
#        PATHS "${SHERPA_ROOT}/lib"
#        REQUIRED)
#
#find_library(PIPER_PHONEMIZE_LIB
#        NAMES piper_phonemize
#        PATHS "${PIPER_ROOT}/build"
#        REQUIRED)
#
#find_library(PIPER_ORT_LIB
#        NAMES onnxruntime
#        PATHS "${PIPER_ORT}"
#        REQUIRED)
#
#set_target_properties(bms_ai PROPERTIES
#        BUILD_RPATH "${SHERPA_ROOT}/lib;${PIPER_ROOT}/build;${PIPER_ROOT}/build/ei/lib;${PIPER_ORT};/Users/zay4ik/CLionProjects/BMS_AI/piper-phonemize/build/ei/lib")
#
#target_link_libraries(bms_ai PRIVATE
#        curl
#        nlohmann_json::nlohmann_json
#        ${SHERPA_CXX_LIB}
#        ${SHERPA_C_LIB}
#        ${ONNXRUNTIME_LIB}
#        ${PIPER_PHONEMIZE_LIB}
#        -L/Users/zay4ik/CLionProjects/BMS_AI/piper-phonemize/build/ei/lib
#        espeak-ng
#        ${PIPER_ORT_LIB})
#endif()
