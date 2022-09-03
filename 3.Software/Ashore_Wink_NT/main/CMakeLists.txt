file(GLOB_RECURSE IMAGE_SOURCES images/*.c)

idf_component_register(SRCS "i80_controller_example_main.c" "lvgl_demo_ui.c"  "./Show_lvgl/Show_lvgl.c" "./Show_lvgl/LVGL_EX.c" ${IMAGE_SOURCES}
                       INCLUDE_DIRS ".")
