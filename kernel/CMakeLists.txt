cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(ds4touch_kernel)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3 -std=gnu99 -DTAIPOOL_AS_STDLIB")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti -fno-exceptions")

include_directories(
)

link_directories(
	${CMAKE_CURRENT_BINARY_DIR}
)

option(RELEASE "Release build" OFF)

if (RELEASE)
	add_definitions(-DRELEASE)
endif(RELEASE)

add_executable(${PROJECT_NAME}
	src/main.c
	src/log.c
)

target_link_libraries(${PROJECT_NAME}
	m
	gcc
	taihenForKernel_stub
	SceSysclibForDriver_stub
	SceSysmemForDriver_stub
	SceSysmemForKernel_stub
	SceThreadmgrForDriver_stub
	SceIofilemgrForDriver_stub
	SceBtForDriver_stub
	SceKernelSuspendForDriver_stub
	SceCtrlForDriver_stub
	SceDebugForDriver_stub
)

set_target_properties(${PROJECT_NAME}
	PROPERTIES LINK_FLAGS "-nostdlib"
	COMPILE_FLAGS "-D__VITA_KERNEL__"
)

vita_create_self(ds4touch.skprx ${PROJECT_NAME}
	UNSAFE
	CONFIG export.yml
)
add_dependencies(ds4touch.skprx ${PROJECT_NAME})
 
vita_create_stubs(stubs ${PROJECT_NAME} export.yml KERNEL)

install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/stubs/
	DESTINATION lib
	FILES_MATCHING PATTERN "*.a"
)
