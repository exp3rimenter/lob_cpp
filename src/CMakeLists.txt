add_library(lob STATIC
    order_book.cpp
    matching_engine.cpp
)

target_include_directories(lob
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(lob
    PUBLIC  lob_compiler_options
    PRIVATE spdlog::spdlog
)

set_target_properties(lob PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
