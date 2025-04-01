#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "target.h"
#include "target_type.h"
#include "register.h"
#include "breakpoints.h"
#include "myNewCPU.h"
#include "helper/log.h"

/* 私有函数声明 */
static int myNewCPU_init_target(struct command_context *cmd_ctx, struct target *target);
static int myNewCPU_examine(struct target *target);
static int myNewCPU_halt(struct target *target);
static int myNewCPU_resume(struct target *target, bool current, target_addr_t address, bool handle_breakpoints, bool debug_execution);
static int myNewCPU_step(struct target *target, bool current, target_addr_t address, bool handle_breakpoints);
static int myNewCPU_add_breakpoint(struct target *target, struct breakpoint *breakpoint);
static int myNewCPU_remove_breakpoint(struct target *target, struct breakpoint *breakpoint);
static int myNewCPU_read_memory(struct target *target, target_addr_t address, uint32_t size, uint32_t count, uint8_t *buffer);
static int myNewCPU_write_memory(struct target *target, target_addr_t address, uint32_t size, uint32_t count, const uint8_t *buffer);
static int myNewCPU_get_gdb_reg_list(struct target *target, struct reg **reg_list[], int *reg_list_size, enum target_register_class reg_class);
static int myNewCPU_poll(struct target *target);

/* 实现这些函数... */
static int myNewCPU_init_target(struct command_context *cmd_ctx, struct target *target)
{
    struct myNewCPU_common *myNewCPU = calloc(1, sizeof(struct myNewCPU_common));
    if (!myNewCPU)
        return ERROR_FAIL;
    
    target->arch_info = myNewCPU;
    myNewCPU->target = target;
    myNewCPU->common_magic = 0x12345678; /* 特定标识 */
    
    return ERROR_OK;
}

/* 实现其他函数... */

/* 导出目标类型 */
struct target_type myNewCPU_target = {
    .name = "myNewCPU",
    
    // .target_create = myNewCPU_target_create,
    .target_create = NULL,
    .init_target = myNewCPU_init_target,
    .examine = myNewCPU_examine,
    
    .poll = myNewCPU_poll,
    // .poll = NULL,
    .halt = myNewCPU_halt,
    .resume = myNewCPU_resume,
    .step = myNewCPU_step,
    
    // .assert_reset = myNewCPU_assert_reset,
    // .deassert_reset = myNewCPU_deassert_reset,
    .assert_reset = NULL,
    .deassert_reset = NULL,
    

    .read_memory = myNewCPU_read_memory,
    .write_memory = myNewCPU_write_memory,
    
    .add_breakpoint = myNewCPU_add_breakpoint,
    .remove_breakpoint = myNewCPU_remove_breakpoint,
    
    .get_gdb_reg_list = myNewCPU_get_gdb_reg_list,
    
    /* 其他函数... */
};

#include "../src/jtag/jtag.h"
#include "../src/helper/time_support.h"
int jtag_read_idcode(struct jtag_tap *tap, uint32_t *idcode, int verify) {
    uint8_t out[4] = {0}; // IDCODE请求（全0）
    uint8_t in[4];
    struct scan_field field = {
        .num_bits = 32,
        .out_value = out,
        .in_value = in
    };

    // 切换到JTAG DR-Scan流程
    jtag_add_dr_scan(tap, 1, &field, TAP_IDLE);
    if (jtag_execute_queue() != ERROR_OK)
        return ERROR_FAIL;

    *idcode = buf_get_u32(in, 0, 32);
    
    // 可选：验证IDCODE是否有效
    if (verify && (*idcode == 0 || *idcode == 0xFFFFFFFF)) {
        LOG_ERROR("Invalid IDCODE: 0x%08x", *idcode);
        return ERROR_FAIL;
    }

    return ERROR_OK;
}

// static int read_dtmcs(struct jtag_tap *tap, uint32_t *dtmcs) {
//     // 发送DTMCS IR指令(0x10)
//     uint8_t dtmcs_ir = 0x10;
//     struct scan_field ir_field = {
//         .num_bits = tap->ir_length,
//         .out_value = &dtmcs_ir,
//         .in_value = NULL,
//         .check_value = NULL,
//         .check_mask = NULL
//     };
//     jtag_add_ir_scan(tap, &ir_field, TAP_IDLE);

//     // 读取DTMCS DR值
//     struct scan_field dr_field = {
//         .num_bits = 32,
//         .out_value = NULL,
//         .in_value = (uint8_t *)dtmcs,
//         .check_value = NULL,
//         .check_mask = NULL
//     };
//     jtag_add_dr_scan(tap, 1, &dr_field, TAP_IDLE);

//     return jtag_execute_queue();
// }

// static void analyze_dtmcs(uint32_t dtmcs) {
//     // 按照精确位域解析
//     bool dmihardreset = (dtmcs >> 17) & 0x1;
//     bool dmireset = (dtmcs >> 16) & 0x1;
//     uint8_t idle = (dtmcs >> 10) & 0x1F;  // 5 bits
//     uint8_t dmistat = (dtmcs >> 8) & 0x3; // 2 bits
//     uint8_t abits = (dtmcs >> 4) & 0xF;   // 4 bits
//     uint8_t version = dtmcs & 0xF;        // 4 bits

//     LOG_INFO("DTMCS Register: 0x%08x", dtmcs);
//     LOG_INFO("  Version: 0x%x (%s)", version, 
//             version == 1 ? "Debug 0.13" : "Unknown");
//     LOG_INFO("  Address bits: %d", abits);
//     LOG_INFO("  Idle cycles: %d", idle);
//     LOG_INFO("  Status: %d (%s)", dmistat,
//             dmistat == 0 ? "no error" : "error");
//     LOG_INFO("  DMIRESET: %d, DMIHARDRESET: %d", 
//             dmireset, dmihardreset);
// }

static int myNewCPU_poll(struct target *target)
{
    // struct jtag_tap *tap = target->tap;
    // if (!tap) {
    //     LOG_ERROR("TAP not initialized!");
    //     return ERROR_FAIL;
    // }

    // // 准备IR扫描
    // uint8_t idcode_ir = 0x01;  // IDCODE指令码
    // uint8_t ir_in[1] = {0};
    // uint8_t check_value = 0x01;  // 期望捕获的值
    // uint8_t check_mask = 0x1f;   // 有效位掩码

    // struct scan_field ir_field = {
    //     .num_bits = tap->ir_length,
    //     .out_value = &idcode_ir,
    //     .in_value = ir_in,
    //     .check_value = &check_value,
    //     .check_mask = &check_mask
    // };

    // // 执行IR扫描
    // jtag_add_ir_scan(tap, &ir_field, TAP_IDLE);
    
    // // 准备DR扫描（读取IDCODE）
    // uint32_t idcode = 0;
    // struct scan_field dr_field = {
    //     .num_bits = 32,
    //     .out_value = NULL,  // 纯读取操作
    //     .in_value = (uint8_t *)&idcode,
    //     .check_value = NULL,
    //     .check_mask = NULL
    // };

    // jtag_add_dr_scan(tap, 1, &dr_field, TAP_IDLE);
    
    // if (jtag_execute_queue() != ERROR_OK) {
    //     LOG_ERROR("JTAG scan failed");
    //     return ERROR_FAIL;
    // }

    // // 验证IR扫描结果
    // if ((ir_in[0] & check_mask) != check_value) {
    //     LOG_ERROR("IR scan failed: got 0x%02x (expected 0x%02x under mask 0x%02x)",
    //              ir_in[0], check_value, check_mask);
    //     return ERROR_FAIL;
    // }

    // uint32_t dtmcs;
    // if (read_dtmcs(tap, &dtmcs) == ERROR_OK) {
    //     analyze_dtmcs(dtmcs);
        
    //     // 检查调试模块是否就绪
    //     if (((dtmcs >> 8) & 0x3) != 0) {
    //         LOG_WARNING("Debug module reports error state");
    //     }
    // }
    // LOG_INFO("[%ld ms]Polled IDCODE: 0x%08x",timeval_ms(), idcode);
    // target->state = TARGET_RUNNING;
    LOG_INFO("[%ld ms] in target.poll",timeval_ms());
    return ERROR_OK;
}
static int myNewCPU_examine(struct target *target)
{
    (void)(target);
    return ERROR_OK;
}

static int myNewCPU_halt(struct target *target)
{
    LOG_INFO("in myNewCPU_halt");
    (void)(target);
    return ERROR_OK;
}

static int myNewCPU_resume(struct target *target, bool current, target_addr_t address, 
    bool handle_breakpoints, bool debug_execution)
{
    (void)(target);
    (void)(current);
    (void)(address);
    (void)(handle_breakpoints);
    (void)(debug_execution);
    return ERROR_OK;
}

static int myNewCPU_step(struct target *target, bool current, target_addr_t address, 
    bool handle_breakpoints)
{
    (void)(target);
    (void)(current);
    (void)(address);
    (void)(handle_breakpoints);
    return ERROR_OK;
}

static int myNewCPU_add_breakpoint(struct target *target, struct breakpoint *breakpoint)
{
    (void)(target);
    (void)(breakpoint);
    return ERROR_OK;
}

static int myNewCPU_remove_breakpoint(struct target *target, struct breakpoint *breakpoint)
{
    (void)(target);
    (void)(breakpoint);
    return ERROR_OK;
}

static int myNewCPU_read_memory(struct target *target, target_addr_t address, 
                                uint32_t size, uint32_t count, uint8_t *buffer)
{
    (void)(target);
    (void)(address);
    (void)(size);
    (void)(count);
    (void)(buffer);
    return ERROR_OK;
}

static int myNewCPU_write_memory(struct target *target, target_addr_t address, 
                                 uint32_t size, uint32_t count, const uint8_t *buffer)
{
    (void)(target);
    (void)(address);
    (void)(size);
    (void)(count);
    (void)(buffer);
    return ERROR_OK;
}

static int myNewCPU_get_gdb_reg_list(struct target *target, struct reg **reg_list[],
                                     int *reg_list_size, enum target_register_class reg_class)
{
    (void)(target);
    (void)(reg_list);
    (void)(reg_list_size);
    (void)(reg_class);
    return ERROR_OK;
}
