include_directories(${LLVM_MAIN_SRC_DIR}/projects/axtor/include)

add_llvm_library(LLVMPIR
  SequentializePIR.cpp
  PIRToOpenMP.cpp
  PIRToOpenCL.cpp
  PIR.cpp

  DEPENDS
  intrinsics_gen

  LINK_LIBS
  ${LLVM_MAIN_SRC_DIR}/projects/axtor/build/lib/libAxtor.so
  ${LLVM_MAIN_SRC_DIR}/projects/axtor/build/lib/libAxtor_OpenCL.so
 )
