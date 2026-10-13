add_executable(Injector
    main.cpp
)
target_link_libraries(Injector PRIVATE user32 kernel32)
