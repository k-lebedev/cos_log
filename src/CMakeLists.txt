aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SOURCES)
add_library(cos_log STATIC ${SOURCES})
target_compile_options(cos_log PRIVATE -Wall -Wextra -Wconversion -Wshadow)
target_compile_definitions(cos_log PRIVATE -D_XOPEN_SOURCE=700)

if (DO_LOG_FUNCTION_NAME)
    target_compile_definitions(cos_log PRIVATE -DDO_LOG_FUNCTION_NAME=1)
else(DO_LOG_FUNCTION_NAME)
    target_compile_definitions(cos_log PRIVATE -DDO_LOG_FUNCTION_NAME=0)
endif(DO_LOG_FUNCTION_NAME)

if (DO_LOG_CURRENT_TIME)
    target_compile_definitions(cos_log PRIVATE -DDO_LOG_CURRENT_TIME=1)
else(DO_LOG_CURRENT_TIME)
    target_compile_definitions(cos_log PRIVATE -DDO_LOG_CURRENT_TIME=0)
endif(DO_LOG_CURRENT_TIME)

target_include_directories(cos_log PUBLIC ${PROJECT_SOURCE_DIR}/include)
add_library(sub::cos_log ALIAS cos_log)