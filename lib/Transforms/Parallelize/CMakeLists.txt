add_llvm_library(LLVMParallelize
  FlattenPIR.cpp
  Sequentialize.cpp
  OpenMP.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  )

add_dependencies(LLVMParallelize intrinsics_gen)
