add_llvm_library(LLVMParallelize
  Sequentialize.cpp
  OpenMP.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms
  )

add_dependencies(LLVMParallelize intrinsics_gen)
