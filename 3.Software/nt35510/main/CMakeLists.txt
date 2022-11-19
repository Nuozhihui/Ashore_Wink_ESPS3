file(GLOB_RECURSE IMG_SOURCES ./images/*.c)
file(GLOB_RECURSE FONT_SOURCES ./fonts/*.c)

idf_component_register(SRCS 
                        ${IMG_SOURCES}
                        ${FONT_SOURCES}
                            "main.c"
                            "esp_lcd_panel_nt35510_custom.c"
                            "lvgl_gui.c"
                            "label_app.c"
                            "start_app.c"
                            "http_req.c"
                            "aht20.c"
                            "rest_server.c"
                    INCLUDE_DIRS ".")
