# CMakeList.txt : CMake lists for WaveReader

cmake_minimum_required (VERSION 3.8)

# Add source to library.
add_library (WaveHandler INTERFACE)
target_include_directories(WaveHandler INTERFACE WaveHandler/)
