cmake_minimum_required(VERSION 3.22)

set(SRCS "")
set(INCS "")

project(msgbus)

list(APPEND SRCS  "msgbus/msgbus.c"
        "msgbus/rbtree.c"
        )

list (APPEND INCS "msgbus/")


list(APPEND SRCS  "sample/main.c"
        )

include_directories(${INCS})

add_executable(${PROJECT_NAME} ${SRCS}) 

target_link_libraries(${PROJECT_NAME} pthread) # 链接库
