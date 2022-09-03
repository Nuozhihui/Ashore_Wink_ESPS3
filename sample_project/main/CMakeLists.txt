file(GLOB_RECURSE IMAGE_SOURCES images/*.c)
idf_component_register(SRCS 

                        "main.c"
                        "./images/esp_logo.c" 
                        "./images/esp_text.c" 
                        "lvgl_demo_ui.c" 
                        ${IMAGE_SOURCES}

                    INCLUDE_DIRS ".")
