# Only build Vulkan support if Vulkan is available
find_package(Vulkan QUIET)

message(STATUS "Vulkan_FOUND: ${Vulkan_FOUND}")
if(Vulkan_FOUND)
    message(STATUS "Vulkan_VERSION: ${Vulkan_VERSION}")
    message(STATUS "Vulkan_INCLUDE_DIRS: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "Vulkan_LIBRARIES: ${Vulkan_LIBRARIES}")
endif()

if(NOT Vulkan_FOUND)
    message(STATUS "Vulkan not found, skipping Vulkan support")
    return()
endif()

message(STATUS "Building Vulkan support (platformsupport)")

# VMA (Vulkan Memory Allocator) - header-only library
# Check if system has VMA installed, otherwise use bundled version
find_path(VMA_INCLUDE_DIR vk_mem_alloc.h
    HINTS
        /usr/include
        /usr/local/include
        $ENV{VULKAN_SDK}/include
)

if(VMA_INCLUDE_DIR)
    message(STATUS "Found VMA at ${VMA_INCLUDE_DIR}")
else()
    message(STATUS "VMA not found in system, using bundled version")
endif()

message(STATUS "Adding Vulkan sources to kwin target")
target_sources(kwin PRIVATE
    # Core Vulkan backend (not matched in OpenGL)
    vulkanbackend.cpp

    # Vulkan-specific infrastructure (not matched in OpenGL)
    vulkanrenderpass.cpp
    vulkanrendertarget.cpp

    # X11-specific surface texture (not matched in OpenGL)
    vulkansurfacetexture_x11.cpp
)

target_link_libraries(kwin PRIVATE
    Vulkan::Vulkan
    XCB::DRI3
    Libdrm::Libdrm
)

# Add VMA include directory
target_include_directories(kwin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
