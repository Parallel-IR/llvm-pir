add_llvm_library(LLVMPIR
  SequentializePIR.cpp
  PIRToOpenMP.cpp
  PIR.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms

  DEPENDS
  intrinsics_gen
 )
