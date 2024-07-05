idf_component_register(
    SRCS "focaltech_touch.cpp" "main.cpp" "flutter_remote_display.c"
    INCLUDE_DIRS "."
)

target_compile_options(
    ${COMPONENT_LIB} PRIVATE -Werror -Wall -Wextra
)

set_source_files_properties(
    "main.cpp" "flutter_remote_display.c"
    PROPERTIES COMPILE_FLAGS -Werror -Wall -Wextra
)
