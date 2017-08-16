cd $PWD
mkdir -p $1_out
cd $1_out

rm -f $1.ll $1_omp.ll $1.bc utils.ll utils.bc $1_linked.bc for_linked.o $1.out

${CLANG_BIN_DIR}/clang++ -std=c++14 -S -fopenmp -emit-llvm ../$1.cpp -I${CLANG_BIN_DIR}/../projects/openmp/runtime/src/ -o $1.ll
${LLVM_BIN_DIR}/opt -S -mem2reg -pir2omp $1.ll -o $1_omp.ll
${LLVM_BIN_DIR}/llvm-as $1_omp.ll

${CLANG_BIN_DIR}/clang++ -std=c++14 -S -fopenmp -emit-llvm ../utils.cpp -I${CLANG_BIN_DIR}/../projects/openmp/runtime/src/ -o utils.ll
${LLVM_BIN_DIR}/llvm-as utils.ll

${LLVM_BIN_DIR}/llvm-link $1_omp.bc utils.bc -o $1_linked.bc

${LLVM_BIN_DIR}/llc -filetype=obj $1_linked.bc

gcc $1_linked.o -L${CLANG_BIN_DIR}/../lib -lomp -lstdc++ -o $1.out
