cmake_minimum_required(VERSION 3.0.0)
project(uav-net-2 VERSION 0.1.0)

include(CTest)
enable_testing()

set(CMAKE_SYSROOT /opt/fsl-imx-xwayland/5.4-zeus/sysroots/aarch64-poky-linux)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
   add_definitions(-DCOMPILEDWITHC11)
   message(STATUS "Using flag -std=c++11.")
elseif(COMPILER_SUPPORTS_CXX0X)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
   add_definitions(-DCOMPILEDWITHC0X)
   message(STATUS "Using flag -std=c++0x.")
else()
   message(FATAL_ERROR "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

add_definitions(-DTOPO_TEST)
# add_definitions(-DDEBUG_PRINT_NEIB_PKT)

set(MODULE_CXXFILE
   utils.cpp sys_config.cpp
   dsr_route.cpp topo.cpp
   sdn_cmd.cpp video_stream.cpp)

# add_executable(uav_net_2 main.cpp ${MODULE_CXXFILE})
# target_link_libraries(uav_net_2 pthread)

add_executable(dsr_route_test test/dsr_route_test.cpp ${MODULE_CXXFILE})
target_link_libraries(dsr_route_test pthread)

add_executable(sys_config_test test/sys_config_test.cpp sys_config.cpp)
target_link_libraries(sys_config_test pthread)

add_executable(topo_test test/topo_test.cpp ${MODULE_CXXFILE})
target_link_libraries(topo_test pthread)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
