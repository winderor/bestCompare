# CMake script to auto-increment build version counter
if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" VERSION_CONTENTS)
    string(REGEX MATCH "VERSION_MAJOR = ([0-9]+);" _ "${VERSION_CONTENTS}")
    set(CURRENT_MAJOR ${CMAKE_MATCH_1})
    string(REGEX MATCH "VERSION_MINOR = ([0-9]+);" _ "${VERSION_CONTENTS}")
    set(CURRENT_MINOR ${CMAKE_MATCH_1})
    string(REGEX MATCH "BUILD_NUMBER = ([0-9]+);" _ "${VERSION_CONTENTS}")
    set(CURRENT_BUILD ${CMAKE_MATCH_1})
    
    if(NOT CURRENT_MAJOR)
        set(CURRENT_MAJOR 1)
    endif()
    if(NOT CURRENT_MINOR)
        set(CURRENT_MINOR 0)
    endif()
    if(NOT CURRENT_BUILD)
        set(CURRENT_BUILD 1)
    endif()
    
    math(EXPR NEW_BUILD "${CURRENT_BUILD} + 1")
else()
    set(CURRENT_MAJOR 1)
    set(CURRENT_MINOR 0)
    set(NEW_BUILD 1)
endif()

file(WRITE "${VERSION_FILE}" "#ifndef BESTCOMPARE_VERSION_HPP
#define BESTCOMPARE_VERSION_HPP

namespace BestCompare {
    static constexpr int VERSION_MAJOR = ${CURRENT_MAJOR};
    static constexpr int VERSION_MINOR = ${CURRENT_MINOR};
    static constexpr int BUILD_NUMBER = ${NEW_BUILD};
    static constexpr float AppVersion = ${CURRENT_MAJOR}.${CURRENT_MINOR}f;
}

#endif // BESTCOMPARE_VERSION_HPP
")
