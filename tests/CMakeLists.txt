add_executable(tst_qlogger tst_qlogger.cpp)

target_link_libraries(tst_qlogger PRIVATE
    QLogger
    Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME qlogger COMMAND tst_qlogger)
