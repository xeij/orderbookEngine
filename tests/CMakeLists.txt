include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.3
)
FetchContent_MakeAvailable(Catch2)

add_executable(lob_tests
    test_intrusive_list.cpp
    test_slab_allocator.cpp
    test_order_id_map.cpp
    test_bitmap.cpp
    test_order_book.cpp
    test_matching.cpp
    test_tif.cpp
    test_spsc_ring.cpp
)
target_link_libraries(lob_tests PRIVATE lob_core Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(lob_tests)
