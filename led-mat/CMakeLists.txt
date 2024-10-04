add_executable(led-mat
        led-mat.c
        led-server.c
        font.c
        )

# Generate PIO headers
pico_generate_pio_header(led-mat ${CMAKE_CURRENT_LIST_DIR}/hub75.pio)

target_include_directories(led-mat PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        )

# pull in common dependencies
target_link_libraries(led-mat
                      pico_stdlib
                      hardware_pio
                      hardware_adc
                      pico_cyw43_arch_lwip_poll
)

# create map/bin/hex file etc.
pico_add_extra_outputs(led-mat)

# add url via pico_set_program_url
example_auto_set_url(led-mat)
