idf_component_register(
    SRCS "main.c" "app.c" "gattcomm.c" "sppcomm.c" "ledmgr.c"
    PRIV_REQUIRES bt nvs_flash
    INCLUDE_DIRS "")
