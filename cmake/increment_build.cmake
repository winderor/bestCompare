# CMake script to auto-increment build version counter
if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" VERSION_CONTENTS)
    string(REGEX MATCH "BUILD_NUMBER = ([0-9]+);" _ "${VERSION_CONTENTS}")
    set(CURRENT_BUILD ${CMAKE_MATCH_1})
    if(NOT CURRENT_BUILD)
        set(CURRENT_BUILD 1)
    endif()
    math(EXPR NEW_BUILD "${CURRENT_BUILD} + 1")
else()
    set(NEW_BUILD 1)
endif()

math(EXPR MINOR_VER "10 + ${NEW_BUILD}")
set(NEW_FLOAT_VERSION "0.${MINOR_VER}")

file(WRITE "${VERSION_FILE}" "#ifndef BESTCOMPARE_VERSION_HPP
#define BESTCOMPARE_VERSION_HPP

namespace BestCompare {
    static constexpr int VERSION_MAJOR = 0;
    static constexpr int VERSION_MINOR = 1;
    static constexpr int BUILD_NUMBER = ${NEW_BUILD};
    static constexpr float AppVersion = ${NEW_FLOAT_VERSION}f;
}

#endif // BESTCOMPARE_VERSION_HPP
")
