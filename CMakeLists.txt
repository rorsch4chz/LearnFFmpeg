cmake_minimum_required(VERSION 3.26)
project(LearnFFmpeg C)

set(CMAKE_C_STANDARD 11)

set(FFMPEG_DIR /usr/local/ffmpeg)

# 头文件搜索路径
include_directories(${FFMPEG_DIR}/include/)
# 动态链接库或静态链接库的搜索路径
link_directories(${FFMPEG_DIR}/lib/)

add_executable(LearnFFmpeg main.c
        testc.h
        testc.c)

#链接库
target_link_libraries(
        LearnFFmpeg
        #FFmpeg 库
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        swresample
        swscale
)
