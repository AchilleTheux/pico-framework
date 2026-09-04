# pwm - one PWM output on a GPIO, set by frequency and duty or by pulse width.
#
# pwm_timing.c is the arithmetic the SDK leaves to the caller: a divider and a
# wrap for a named frequency, and a channel level for a duty cycle or a pulse
# width. Integer-only and free of the SDK, so it compiles straight into the
# host tests (DESIGN_DOC.md section 19) -- which is where it belongs, because
# the alternative way to check a frequency calculation is an oscilloscope.
#
# pwm.c is the register writing around it, plus the one piece of care this
# hardware needs: the two channels of a slice share a wrap and a divider, so
# initialising an output cannot quietly move another one's frequency.

add_library(pico_framework_pwm STATIC
    pwm_timing.c
    pwm.c
)

add_library(pico_framework::pwm ALIAS pico_framework_pwm)

target_include_directories(pico_framework_pwm PUBLIC
    include
)

pico_framework_set_warnings(pwm_timing.c pwm.c)

target_link_libraries(pico_framework_pwm PUBLIC
    pico_stdlib
    hardware_pwm
    hardware_clocks
)
