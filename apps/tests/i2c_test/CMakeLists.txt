# Interactive bench for the i2c_device component. See README.md.

add_executable(app_i2c_test
    main.c
)

set(I2C_TEST_INSTANCE 0      CACHE STRING "I2C instance, 0 or 1")
set(I2C_TEST_SDA_PIN  4      CACHE STRING "SDA pin")
set(I2C_TEST_SCL_PIN  5      CACHE STRING "SCL pin")
set(I2C_TEST_BAUDRATE 100000 CACHE STRING "Bus rate")
option(I2C_TEST_INTERNAL_PULLUPS "Enable the pads' weak internal pull-ups" ON)

target_compile_definitions(app_i2c_test PRIVATE
    I2C_TEST_INSTANCE=${I2C_TEST_INSTANCE}
    I2C_TEST_SDA_PIN=${I2C_TEST_SDA_PIN}
    I2C_TEST_SCL_PIN=${I2C_TEST_SCL_PIN}
    I2C_TEST_BAUDRATE=${I2C_TEST_BAUDRATE}
    I2C_TEST_INTERNAL_PULLUPS=$<BOOL:${I2C_TEST_INTERNAL_PULLUPS}>
)

target_link_libraries(app_i2c_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::i2c_device
    pico_framework::vl53l0x
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_i2c_test 1)
pico_enable_stdio_uart(app_i2c_test 1)

pico_add_extra_outputs(app_i2c_test)
