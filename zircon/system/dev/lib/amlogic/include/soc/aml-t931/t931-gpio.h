// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <soc/aml-s905d2/s905d2-gpio.h>

// GPIOs are identical to S905D2
#define T931_GPIOZ(n)       S905D2_GPIOZ(n)
#define T931_GPIOA(n)       S905D2_GPIOA(n)
#define T931_GPIOBOOT(n)    S905D2_GPIOBOOT(n)
#define T931_GPIOC(n)       S905D2_GPIOC(n)
#define T931_GPIOX(n)       S905D2_GPIOX(n)
#define T931_GPIOH(n)       S905D2_GPIOH(n)
#define T931_GPIOAO(n)      S905D2_GPIOAO(n)
#define T931_GPIOE(n)       S905D2_GPIOE(n)

#define T931_GPIOZ_2_TDMC_D0_FN            S905D2_GPIOZ_2_TDMC_D0_FN
#define T931_GPIOZ_3_TDMC_D1_FN            S905D2_GPIOZ_3_TDMC_D1_FN
#define T931_GPIOZ_4_TDMC_D2_FN            S905D2_GPIOZ_4_TDMC_D2_FN
#define T931_GPIOZ_5_TDMC_D3_FN            S905D2_GPIOZ_5_TDMC_D3_FN
#define T931_GPIOZ_6_TDMC_FS_FN            S905D2_GPIOZ_6_TDMC_FS_FN
#define T931_GPIOZ_7_TDMC_SCLK_FN          S905D2_GPIOZ_7_TDMC_SCLK_FN

#define T931_GPIOAO_9_MCLK_FN              S905D2_GPIOAO_9_MCLK_FN

#define T931_GPIOA_7_PDM_DCLK_FN           S905D2_GPIOA_7_PDM_DCLK_FN
#define T931_GPIOA_8_PDM_DIN0_FN           S905D2_GPIOA_8_PDM_DIN0_FN
#define T931_GPIOA_9_PDM_DIN1_FN           S905D2_GPIOA_9_PDM_DIN1_FN

#define T931_AO_PAD_DS_A                   S905D2_AO_PAD_DS_A
#define T931_AO_PAD_DS_B                   S905D2_AO_PAD_DS_B
#define T931_PAD_DS_REG1A                  S905D2_PAD_DS_REG1A
#define T931_PAD_DS_REG2A                  S905D2_PAD_DS_REG2A
#define T931_PAD_DS_REG2B                  S905D2_PAD_DS_REG2B
#define T931_PAD_DS_REG3A                  S905D2_PAD_DS_REG3A
#define T931_PAD_DS_REG4A                  S905D2_PAD_DS_REG4A
#define T931_PAD_DS_REG5A                  S905D2_PAD_DS_REG5A

// GPIO Pins used for board rev detection
#define T931_GPIO_HW_ID0                   S905D2_GPIOA(11)
#define T931_GPIO_HW_ID1                   S905D2_GPIOA(12)
#define T931_GPIO_HW_ID2                   S905D2_GPIOC(6)
#define T931_GPIO_HW_ID3                   S905D2_GPIOC(4)
#define T931_GPIO_HW_ID4                   S905D2_GPIOC(5)
