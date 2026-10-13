add_executable(Injector
    main.cpp
)
target_link_libraries(Injector PRIVATE user32 kernel32 advapi32)

if(MSVC)
    set_target_properties(Injector PROPERTIES 
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()
