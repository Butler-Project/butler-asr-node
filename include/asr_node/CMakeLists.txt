# This file is consumed from the ROS 2 package root with add_subdirectory().
# It defines the reusable header-only ASR support target.

set(ASR_NODE_PACKAGE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(ASR_NODE_PACKAGE_INCLUDE_DIR "${ASR_NODE_PACKAGE_ROOT}/include")
set(ASR_NODE_INTERNAL_INCLUDE_DIR "${ASR_NODE_PACKAGE_ROOT}/include/asr_node")
set(ASR_NODE_RESOURCE_DIR "${ASR_NODE_PACKAGE_ROOT}/resources")
set(ASR_NODE_THIRD_PARTY_DIR "${ASR_NODE_RESOURCE_DIR}/third_party")

set(ONNXRUNTIME_INCLUDE_DIR
  "/opt/onnxruntime/onnxruntime-linux-x64-1.24.4/include"
  CACHE PATH "ONNX Runtime include directory"
)
set(ONNXRUNTIME_LIB_DIR
  "${ASR_NODE_THIRD_PARTY_DIR}/ort-openvino/lib"
  CACHE PATH "ONNX Runtime/OpenVINO library directory"
)
set(ONNXRUNTIME_SHIM_LIB_DIR
  "${ASR_NODE_THIRD_PARTY_DIR}/ort-openvino-shim/lib"
  CACHE PATH "ONNX Runtime shim library directory"
)
set(SHERPA_ONNX_INCLUDE_DIR
  "${ASR_NODE_PACKAGE_ROOT}/../../resources/sherpa-onnx/shared/include"
  CACHE PATH "sherpa-onnx include directory"
)
set(SHERPA_ONNX_LIB_DIR
  "${ASR_NODE_THIRD_PARTY_DIR}/sherpa-onnx-shared/lib"
  CACHE PATH "sherpa-onnx library directory"
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RTAUDIO REQUIRED rtaudio)

add_library(asr_node_headers INTERFACE)
target_compile_features(asr_node_headers INTERFACE cxx_std_23)
target_include_directories(asr_node_headers INTERFACE
  $<BUILD_INTERFACE:${ASR_NODE_PACKAGE_INCLUDE_DIR}>
  $<BUILD_INTERFACE:${ASR_NODE_INTERNAL_INCLUDE_DIR}>
  $<BUILD_INTERFACE:${ONNXRUNTIME_INCLUDE_DIR}>
  $<BUILD_INTERFACE:${SHERPA_ONNX_INCLUDE_DIR}>
  $<INSTALL_INTERFACE:include>
  ${RTAUDIO_INCLUDE_DIRS}
)
target_compile_options(asr_node_headers INTERFACE
  ${RTAUDIO_CFLAGS_OTHER}
)
target_link_directories(asr_node_headers INTERFACE
  "${ONNXRUNTIME_SHIM_LIB_DIR}"
  "${ONNXRUNTIME_LIB_DIR}"
  "${SHERPA_ONNX_LIB_DIR}"
)
target_link_libraries(asr_node_headers INTERFACE
  sherpa-onnx-cxx-api
  sherpa-onnx-c-api
  onnxruntime
  cargs
  ${RTAUDIO_LIBRARIES}
)

set(ASR_NODE_RUNTIME_RPATH
  "${ONNXRUNTIME_SHIM_LIB_DIR};${SHERPA_ONNX_LIB_DIR};${ONNXRUNTIME_LIB_DIR}"
  CACHE INTERNAL "Runtime search path for ASR native dependencies"
)
