cd $PWD

rm -f *.bc *.o *.out main.ll merge_sort_serial.ll

${CLANG_BIN_DIR}/clang++ -std=c++11 -S -emit-llvm main.cpp -I${CLANG_BIN_DIR}/../projects/openmp/runtime/src/ -o main.ll
${LLVM_BIN_DIR}/llvm-as main.ll

${CLANG_BIN_DIR}/clang++ -std=c++11 -S -emit-llvm merge_sort_serial.cpp -I${CLANG_BIN_DIR}/../projects/openmp/runtime/src/ -o merge_sort_serial.ll
${LLVM_BIN_DIR}/llvm-as merge_sort_serial.ll

${LLVM_BIN_DIR}/opt -pir2omp merge_sort.ll -o merge_sort.bc

${LLVM_BIN_DIR}/llvm-link main.bc merge_sort_serial.bc merge_sort.bc -o linked.bc

${LLVM_BIN_DIR}/llc -filetype=obj linked.bc

gcc linked.o -L${CLANG_BIN_DIR}/../lib -lomp -lstdc++ -o linked.out
