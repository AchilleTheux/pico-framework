# Components

Reusable firmware components. Each one is a CMake library target that an
application links explicitly; components are never selected by the user as a
build variable.

Layout (DESIGN_DOC.md section 6):

```text
components/<name>/
├── CMakeLists.txt
├── include/
│   └── <name>.h
└── <name>.c
```

`CMakeLists.txt` defines a target named `pico_framework_<name>` and the alias
`pico_framework::<name>`:

```cmake
add_library(pico_framework_ws2812 STATIC ws2812.c)
add_library(pico_framework::ws2812 ALIAS pico_framework_ws2812)

target_include_directories(pico_framework_ws2812 PUBLIC include)

target_link_libraries(pico_framework_ws2812 PUBLIC
    pico_stdlib
    hardware_pio
)
```

A component that depends on another links it the same way, so applications
never list transitive dependencies:

```cmake
target_link_libraries(pico_framework_ax12 PUBLIC pico_framework::half_duplex_uart)
```

Add the component's name to `PICO_FRAMEWORK_COMPONENTS` in
`cmake/components.cmake`. Registered components are added with
`EXCLUDE_FROM_ALL`, so only those reachable from the selected application are
built.

Components must not hard-code project pins or peripherals; they take explicit
configuration structures from the application (DESIGN_DOC.md section 13).

No components exist yet — they are ported in implementation steps 8 onward.
