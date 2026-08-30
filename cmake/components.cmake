# Registration of reusable components (DESIGN_DOC.md sections 6, 7 and 10).
#
# Every component listed here is added with EXCLUDE_FROM_ALL, so registering
# one costs nothing until an application links it. Only components reachable
# from the selected application's target_link_libraries() are built.
#
# The list is deliberately explicit rather than globbed: adding a component is
# a two-line change, and an explicit list keeps the set of framework targets
# reviewable.
#
# To add a component:
#   1. create components/<name>/ with a CMakeLists.txt defining
#      pico_framework_<name> and the alias pico_framework::<name>
#   2. append <name> to PICO_FRAMEWORK_COMPONENTS below
#
# No components exist yet; they are ported in later implementation steps.
set(PICO_FRAMEWORK_COMPONENTS
    # ws2812
    # cli
    # half_duplex_uart
    # ax12
    # feetech
    # wifi
)

function(pico_framework_add_components)
    foreach(component IN LISTS PICO_FRAMEWORK_COMPONENTS)
        if(NOT EXISTS "${PICO_FRAMEWORK_ROOT}/components/${component}/CMakeLists.txt")
            message(FATAL_ERROR
                "Component '${component}' is registered in "
                "cmake/components.cmake but components/${component}/CMakeLists.txt "
                "does not exist.")
        endif()
        add_subdirectory("${PICO_FRAMEWORK_ROOT}/components/${component}"
                         "${CMAKE_BINARY_DIR}/components/${component}"
                         EXCLUDE_FROM_ALL)
    endforeach()
endfunction()
