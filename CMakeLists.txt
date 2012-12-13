project (aike)
cmake_minimum_required (VERSION 2.8)

# Get LLVM config
set(LLVM_ROOT ${CMAKE_CURRENT_LIST_DIR}/extern/llvm CACHE PATH "Root of LLVM install.")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${LLVM_ROOT}/share/llvm/cmake")
include(LLVMConfig)

# Setup LLVM paths
# include_directories( ${LLVM_INCLUDE_DIRS} )
# link_directories( ${LLVM_LIBRARY_DIRS} )
include_directories( ${LLVM_ROOT}/include )
link_directories( ${LLVM_ROOT}/lib )
add_definitions( ${LLVM_DEFINITIONS} )

# Include JIT compiler
llvm_map_components_to_libraries(REQ_LLVM_LIBRARIES jit interpreter native)

# Bootstrap project
add_executable(bootstrap bootstrap/bootstrap.cpp bootstrap/lexer.cpp)
target_link_libraries(bootstrap ${REQ_LLVM_LIBRARIES})
