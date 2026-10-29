// main/cha_pool.c —— 加图：png 丢进 ~/work/assets/src，cd 进去跑 convert_cha.sh 72 64 cha xxx.png，转到 main/assets/images/cha_xxx.c，然后下面数组加一行（该目录整目录自动编译）
#include "cha_pool.h"
#include "esp_random.h"
LV_IMAGE_DECLARE(cha_box);
LV_IMAGE_DECLARE(cha_ctrl_c);
LV_IMAGE_DECLARE(cha_ctrl_v);
LV_IMAGE_DECLARE(cha_dinosaur);
LV_IMAGE_DECLARE(cha_duck);
LV_IMAGE_DECLARE(cha_eatwatermelon);
LV_IMAGE_DECLARE(cha_flower);
LV_IMAGE_DECLARE(cha_goldcha);
LV_IMAGE_DECLARE(cha_happy);
LV_IMAGE_DECLARE(cha_helloworld);
LV_IMAGE_DECLARE(cha_love);
LV_IMAGE_DECLARE(cha_music);
LV_IMAGE_DECLARE(cha_nonsense);
LV_IMAGE_DECLARE(cha_pencil);
LV_IMAGE_DECLARE(cha_photograph);
LV_IMAGE_DECLARE(cha_school);
LV_IMAGE_DECLARE(cha_sleep);
LV_IMAGE_DECLARE(cha_spirit);
LV_IMAGE_DECLARE(cha_stone);
LV_IMAGE_DECLARE(cha_svg);
LV_IMAGE_DECLARE(cha_work);
static const lv_image_dsc_t *POOL[] = {
    &cha_box,
    &cha_ctrl_c,
    &cha_ctrl_v,
    &cha_dinosaur,
    &cha_duck,
    &cha_eatwatermelon,
    &cha_flower,
    &cha_goldcha,
    &cha_happy,
    &cha_helloworld,
    &cha_love,
    &cha_music,
    &cha_nonsense,
    &cha_pencil,
    &cha_photograph,
    &cha_school,
    &cha_sleep,
    &cha_spirit,
    &cha_stone,
    &cha_svg,
    &cha_work,
};
#define N (sizeof POOL / sizeof POOL[0])
static int s_pick = -1;
const lv_image_dsc_t *cha_random_72(void)
{
    int n = 0;
    if (N > 1) { do { n = esp_random() % N; } while (n == s_pick); }
    s_pick = n;
    return POOL[s_pick];
}
const lv_image_dsc_t *cha_reroll_72(void) { return cha_random_72(); }
