add_compile_options_disable_optimization()

ADD_SUBDIRECTORY(dockey)
ADD_SUBDIRECTORY(engine_error)
ADD_SUBDIRECTORY(error_map_sanity_check)
ADD_SUBDIRECTORY(executor)
ADD_SUBDIRECTORY(histograms)
ADD_SUBDIRECTORY(mcbp)
ADD_SUBDIRECTORY(memory_tracking_test)
ADD_SUBDIRECTORY(scripts_tests)
ADD_SUBDIRECTORY(sizes)
ADD_SUBDIRECTORY(statistics)
ADD_SUBDIRECTORY(testapp)
add_subdirectory(testapp_cluster)
ADD_SUBDIRECTORY(topkeys)
ADD_SUBDIRECTORY(unsigned_leb128)

add_test(NAME kv-engine-check-header-define-once-guard COMMAND ${PYTHON_EXECUTABLE}
        ${Memcached_SOURCE_DIR}/tests/header_define_once_test.py
        --rootdir ${Memcached_SOURCE_DIR}
        --exclude engines/ep/src/tasks.def.h include/statistics/stats.def.h)
