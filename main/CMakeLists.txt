# 二创玩法：main/apps/ 下的 .c 自动编译
file(GLOB WATCHA_APP_SRCS "${CMAKE_CURRENT_LIST_DIR}/apps/*.c")

# 素材：main/assets/ 存放由源图与 TTF 生成的 C 源。
#   images/  art_* ic_* cha_* logo*（LVGL 图片数据），目录内自动编译。
#   fonts/   font_*（字库），按 Flash 预算逐个列出；未列出的留在磁盘但不编入
#            （font_fusion10.c 就是这种情况，见 tools/check_material.py）。
file(GLOB WATCHA_IMAGE_SRCS "${CMAKE_CURRENT_LIST_DIR}/assets/images/*.c")
set(WATCHA_FONT_SRCS
    "assets/fonts/font_fusion12.c"
    "assets/fonts/font_fusion20.c"
    "assets/fonts/font_ps8.c"
    "assets/fonts/font_ps16.c")

idf_component_register(
    SRCS "main.c"
         "pages.c"
         "pages/page_lab.c"
         "pages/page_home.c"
         "pages/page_fortune.c"
         "pages/page_pomo.c"
         "pages/page_type.c"
         "pages/page_intro.c"
         "pages/page_sync.c"
         "pages/page_guide.c"
         "pages/page_melon.c"
         "pages/page_muyu.c"
         "pages/page_1024.c"
         ${WATCHA_APP_SRCS}
         ${WATCHA_IMAGE_SRCS}
         ${WATCHA_FONT_SRCS}
         "boot_anim.c"
         "cha_pool.c"
         "storage.c"
         "ble_sync.c"
         "sfx.c"
    INCLUDE_DIRS "." "apps"
    REQUIRES bsp esp_timer nvs_flash bt json
)
