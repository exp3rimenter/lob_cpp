add_executable(lob_tests test_order_book.cpp)

target_link_libraries(lob_tests
    PRIVATE
        lob
        GTest::gtest
        GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(lob_tests)
