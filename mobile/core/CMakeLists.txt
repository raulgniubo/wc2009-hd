# WC2009 HD core for the mobile apps (static library + C API in wc09_core.h)
add_library(wc09mobile STATIC
    wc09_core.cpp
    Platform_mobile.cpp
    WC09Render.cpp
    Portable.cpp
)
target_include_directories(wc09mobile PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_link_libraries(wc09mobile PUBLIC core ${CMAKE_DL_LIBS})
if (ANDROID)
    target_link_libraries(wc09mobile PUBLIC GLESv3 EGL log)
elseif (APPLE)
    target_link_libraries(wc09mobile PUBLIC "-framework OpenGLES")
elseif (MELONDS_GLES)
    find_library(GLES_LIB GLESv2 REQUIRED)
    target_link_libraries(wc09mobile PUBLIC ${GLES_LIB})
else()
    # desktop OpenGL through glad (the app loads it once its context exists)
    target_sources(wc09mobile PRIVATE ../../src/frontend/glad/glad.c)
    target_include_directories(wc09mobile PUBLIC ../../src/frontend/glad)
endif()
