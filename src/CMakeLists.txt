add_executable(${CMAKE_PROJECT_NAME} main.cpp)

add_subdirectory(mesh)
add_subdirectory(utils)
add_subdirectory(postprocessing)
add_subdirectory(solver)
add_subdirectory(inputParser)

target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC InputParser MeshGeneration Metrics Logger PostProcessing Solver)
