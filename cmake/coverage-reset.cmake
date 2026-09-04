# Drop every .gcda under DIR. gcov ACCUMULATES counts into an existing one, so
# without this a case deleted since the last run still reads as covered.
file(GLOB_RECURSE _gcda "${DIR}/*.gcda")
if(_gcda)
    file(REMOVE ${_gcda})
endif()
