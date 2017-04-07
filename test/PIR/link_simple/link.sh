cd $PWD

rm -f main.ll main.bc simple.bc linked.bc linked.o linked.out

# Produce llvm bitcode from main.cpp. 
${CLANG_BIN_DIR}/clang++ -S -emit-llvm main.cpp -I${CLANG_BIN_DIR}/../projects/openmp/runtime/src/ -o main.ll
${LLVM_BIN_DIR}/llvm-as main.ll

# Lower simple.ll to OpenMP runtime.
${LLVM_BIN_DIR}/opt -pir2omp simple.ll -o simple.bc

# Link the 2 bitcode files.
${LLVM_BIN_DIR}/llvm-link main.bc simple.bc -o linked.bc

# Compile the linked result to the target object.
${LLVM_BIN_DIR}/llc -filetype=obj linked.bc

# Link the target object file with stdc++ and omp libraries
gcc linked.o -L${CLANG_BIN_DIR}/../lib -lomp -lstdc++ -o linked.out
