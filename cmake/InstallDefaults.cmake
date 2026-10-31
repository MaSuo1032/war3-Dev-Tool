# This script is included by cmake_install.cmake after the binaries are installed.
get_filename_component(_war3_defaults_dir "${CMAKE_CURRENT_LIST_DIR}/../config" ABSOLUTE)
set(_war3_install_dir "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
foreach(_war3_default IN ITEMS JassHookConfig.ini mall_blacklist.txt mall_spoof.txt map_keys.txt)
    if(NOT EXISTS "${_war3_install_dir}/${_war3_default}")
        file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" TYPE FILE
            FILES "${_war3_defaults_dir}/${_war3_default}")
    else()
        message(STATUS "Preserving existing configuration: ${_war3_default}")
    endif()
endforeach()
