#ifndef OPENOCD_TARGET_MYNEWCPU_H
#define OPENOCD_TARGET_MYNEWCPU_H

#include "target.h"
#include "target_type.h"

/* 芯片特定的数据结构 */
struct myNewCPU_common {
    struct target *target;
    uint32_t common_magic;
    /* 其他芯片特定数据 */
};

/* 声明外部可见的函数 */
extern  struct target_type myNewCPU_target;

#endif /* OPENOCD_TARGET_MYNEWCHIP_H */