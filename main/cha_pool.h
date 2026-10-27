// main/cha_pool.h —— 首页头像池（72×64）。每次进首页换一只（和上一只不同），不存 NVS。
#pragma once
#include "lvgl.h"
const lv_image_dsc_t *cha_random_72(void);   // 抽一只（每次调用都换）
const lv_image_dsc_t *cha_reroll_72(void);   // 同上，给长按手动换用
