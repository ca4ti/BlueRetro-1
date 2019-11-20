#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xtensa/hal.h>
#include "sdkconfig.h"
#include "zephyr/types.h"
#include "util.h"
#include "config.h"
#include "adapter.h"
#include "wii.h"
#include "dc.h"

const uint32_t generic_btns_mask[32] = {
    BIT(PAD_LX_LEFT), BIT(PAD_LX_RIGHT), BIT(PAD_LY_DOWN), BIT(PAD_LY_UP),
    BIT(PAD_RX_LEFT), BIT(PAD_RX_RIGHT), BIT(PAD_RY_DOWN), BIT(PAD_RY_UP),
    BIT(PAD_LD_LEFT), BIT(PAD_LD_RIGHT), BIT(PAD_LD_DOWN), BIT(PAD_LD_UP),
    BIT(PAD_RD_LEFT), BIT(PAD_RD_RIGHT), BIT(PAD_RD_DOWN), BIT(PAD_RD_UP),
    BIT(PAD_RB_LEFT), BIT(PAD_RB_RIGHT), BIT(PAD_RB_DOWN), BIT(PAD_RB_UP),
    BIT(PAD_MM), BIT(PAD_MS), BIT(PAD_MT), BIT(PAD_MQ),
    BIT(PAD_LM), BIT(PAD_LS), BIT(PAD_LT), BIT(PAD_LJ),
    BIT(PAD_RM), BIT(PAD_RS), BIT(PAD_RT), BIT(PAD_RJ),
};

static to_generic_t to_generic_func[BT_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    wiiu_to_generic,
    NULL,
    NULL,
    NULL,
};

static from_generic_t from_generic_func[WIRED_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    dc_from_generic,
    NULL,
    NULL,
    NULL,
};

static meta_init_t meta_init_func[WIRED_MAX] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    dc_meta_init,
    NULL,
    NULL,
    NULL,
};

struct generic_ctrl ctrl_input;
struct generic_ctrl ctrl_output[WIRED_MAX_DEV];
struct bt_adapter bt_adapter = {0};
struct wired_adapter wired_adapter = {0};

static uint32_t btn_id_to_btn_idx(uint8_t btn_id) {
    if (btn_id < 32) {
        return 0;
    }
    else if (btn_id >= 32 && btn_id < 64) {
        return 1;
    }
    else if (btn_id >= 64 && btn_id < 96) {
        return 2;
    }
    else {
        return 3;
    }
}

static uint32_t adapter_map_from_axis(struct map_cfg * map_cfg, uint8_t src_mask) {
    uint32_t out_mask = 0;
    //printf("%s\n", __FUNCTION__);
    return out_mask;
}

static uint32_t adapter_map_from_btn(struct map_cfg * map_cfg, uint8_t src_mask, uint32_t src_btn_idx) {
    uint32_t out_mask = BIT(map_cfg->dst_id);
    struct generic_ctrl *out = &ctrl_output[map_cfg->dst_id];
    uint8_t dst = map_cfg->dst_btn;
    uint8_t dst_mask = BIT(dst & 0x1F);
    uint32_t dst_btn_idx = btn_id_to_btn_idx(dst);

    /* Check if mapping dst exist in output */
    if (dst_mask & out->mask[dst_btn_idx]) {
        /* Check if button pressed */
        if (ctrl_input.btns[src_btn_idx].value & src_mask) {
            /* Check if dst is an axis */
            if (dst_mask & out->desc[dst_btn_idx]) {
                /* Dst is Axis */
                uint32_t axis_id = btn_id_to_axis(dst);
                float fvalue = out->axes[axis_id].meta->abs_max
                            * btn_sign(out->axes[axis_id].meta->polarity, dst)
                            * (((float)map_cfg->perc_max)/100);
                int32_t value = (int32_t)fvalue;

                if (abs(value) > abs(out->axes[axis_id].value)) {
                    out->axes[axis_id].value = value;
                }
            }
            else {
                /* Dst is Button */
                out->btns[dst_btn_idx].value |= dst_mask;
            }
        }
        /* Flag this dst for update */
        out->map_mask[dst_btn_idx] |= dst_mask;
    }

    return out_mask;
}

static uint32_t adapter_mapping(struct in_cfg * in_cfg) {
    uint32_t out_mask = 0;

    for (uint32_t i = 0; i < in_cfg->map_size; i++) {
        uint8_t src = in_cfg->map_cfg[i].src_btn;
        uint8_t src_mask = BIT(src & 0x1F);
        uint32_t src_btn_idx = btn_id_to_btn_idx(src);

        /* Check if mapping src exist in input */
        if (src_mask & ctrl_input.mask[src_btn_idx]) {
            if (src_mask & ctrl_input.desc[src_btn_idx]) {
                /* Src is Axis */
                out_mask |= adapter_map_from_axis(&in_cfg->map_cfg[i], src_mask);
            }
            else {
                /* Src is Button */
                out_mask |= adapter_map_from_btn(&in_cfg->map_cfg[i], src_mask, src_btn_idx);
            }
        }
    }
    return out_mask;
}

uint8_t btn_id_to_axis(uint8_t btn_id) {
    switch (btn_id) {
        case PAD_LX_LEFT:
        case PAD_LX_RIGHT:
            return AXIS_LX;
        case PAD_LY_DOWN:
        case PAD_LY_UP:
            return AXIS_LY;
        case PAD_RX_LEFT:
        case PAD_RX_RIGHT:
            return AXIS_RX;
        case PAD_RY_DOWN:
        case PAD_RY_UP:
            return AXIS_RY;
        case PAD_LM:
            return TRIG_L;
        case PAD_RM:
            return TRIG_R;
    }
    return AXIS_NONE;
}

uint32_t axis_to_btn_mask(uint8_t axis) {
    switch (axis) {
        case AXIS_LX:
            return BIT(PAD_LX_LEFT) | BIT(PAD_LX_RIGHT);
        case AXIS_LY:
            return BIT(PAD_LY_DOWN) | BIT(PAD_LY_UP);
        case AXIS_RX:
            return BIT(PAD_RX_LEFT) | BIT(PAD_RX_RIGHT);
        case AXIS_RY:
            return BIT(PAD_RY_DOWN) | BIT(PAD_RY_UP);
        case TRIG_L:
            return BIT(PAD_LM);
        case TRIG_R:
            return BIT(PAD_RM);
    }
    return 0x00000000;
}

int8_t btn_sign(uint32_t polarity, uint8_t btn_id) {
    switch (btn_id) {
        case PAD_LX_RIGHT:
        case PAD_LY_UP:
        case PAD_RX_RIGHT:
        case PAD_RY_UP:
        case PAD_LM:
        case PAD_RM:
            return polarity ? -1 : 1;
        case PAD_LX_LEFT:
        case PAD_LY_DOWN:
        case PAD_RY_DOWN:
        case PAD_RX_LEFT:
            return polarity ? 1 : -1;
    }
    return 1;
}

void adapter_bridge(struct bt_data *bt_data) {
#if 0
    uint8_t data[] = {0x00, 0xFF, 0x00, 0xFF};
    struct ctrl_desc ctrl_test;
    ctrl_test.data[BTNS0].type = U8_TYPE;
    ctrl_test.data[BTNS0].pval.u8 = &data[1];
    ctrl_test.data[BTNS1].type = U16_TYPE;
    ctrl_test.data[BTNS1].pval.u16 = (uint16_t *)&data[2];
    ctrl_test.data[LX_AXIS].type = U32_TYPE;
    ctrl_test.data[LX_AXIS].pval.u32 = (uint32_t *)&data[0];
    printf("btns0: %X btns1: %X lx_axis: %X\n", CTRL_DATA(ctrl_test.data[BTNS0]), CTRL_DATA(ctrl_test.data[BTNS1]), CTRL_DATA(ctrl_test.data[LX_AXIS]));
    wiiu_init_desc(bt_data);
#endif
    uint32_t out_mask = 0;
    uint32_t end, start = xthal_get_ccount();

    //printf("dev_id %d dev_type: %d\n", bt_data->dev_id, bt_data->dev_type);
    if (bt_data->dev_id != BT_NONE && to_generic_func[bt_data->dev_type]) {
        //printf("dev_id %d dev_type: %d\n", bt_data->dev_id, bt_data->dev_type);
        to_generic_func[bt_data->dev_type](bt_data, &ctrl_input);

        meta_init_func[wired_adapter.system_id](ctrl_output);

        out_mask = adapter_mapping(&config.in_cfg[bt_data->dev_id]);

        if (wired_adapter.system_id != WIRED_NONE && from_generic_func[wired_adapter.system_id]) {
            for (uint32_t i = 0; out_mask; i++, out_mask >>= 1) {
                /* this need to reset only what was mapped so adapter_mapping need to provide a btn mask */
                from_generic_func[wired_adapter.system_id](&ctrl_output[i], &wired_adapter.data[i]);
            }
        }
    }
    end = xthal_get_ccount();
    printf("%s process time: %dus\n", __FUNCTION__, (end - start)/CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ);
}

void adapter_init(void) {
    wired_adapter.system_id = WIRED_NONE;
}
