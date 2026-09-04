# Bench for the pwm component. See README.md.

add_executable(app_pwm_test
    main.c
)

set(PWM_TEST_GPIO         10   CACHE STRING "GPIO to drive")
set(PWM_TEST_SECOND_GPIO  11   CACHE STRING "Paired GPIO, for the slice-sharing tests")
set(PWM_TEST_FREQUENCY_HZ 1000 CACHE STRING "Starting frequency in Hz")

target_compile_definitions(app_pwm_test PRIVATE
    PWM_TEST_GPIO=${PWM_TEST_GPIO}u
    PWM_TEST_SECOND_GPIO=${PWM_TEST_SECOND_GPIO}u
    PWM_TEST_FREQUENCY_HZ=${PWM_TEST_FREQUENCY_HZ}u
)

target_link_libraries(app_pwm_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::pwm
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_pwm_test 1)
pico_enable_stdio_uart(app_pwm_test 1)

pico_add_extra_outputs(app_pwm_test)
