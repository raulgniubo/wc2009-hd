add_executable(wc09test main.cpp)
find_library(EGL_LIB EGL REQUIRED)
target_link_libraries(wc09test PRIVATE wc09mobile ${EGL_LIB})
