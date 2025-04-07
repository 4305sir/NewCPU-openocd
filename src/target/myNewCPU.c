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
// 分析并打印DMCONTROL寄存器的详细信息
static void analyze_dmcontrol(uint32_t dmcontrol);
static int myNewCPU_selectHart1(struct target *target);
static int myNewCPU_hawindowsel(struct target *target);
static int myNewCPU_hawindow(struct target *target);

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
static int write_dtmcs(struct jtag_tap *tap, uint32_t dtmcs_value) {
    // 发送DTMCS IR指令(0x10)
    uint8_t dtmcs_ir = 0x10;
    struct scan_field ir_field = {
        .num_bits = tap->ir_length,
        .out_value = &dtmcs_ir,
        .in_value = NULL,
        .check_value = NULL,
        .check_mask = NULL
    };
    jtag_add_ir_scan(tap, &ir_field, TAP_IDLE);

    // 准备写入DTMCS DR值的缓冲区
    uint8_t dtmcs_data[4];
    buf_set_u32(dtmcs_data, 0, 32, dtmcs_value);

    // 写入DTMCS DR值
    struct scan_field dr_field = {
        .num_bits = 32,
        .out_value = dtmcs_data,  // 写入操作，提供输出值
        .in_value = NULL,         // 不需要读取返回值
        .check_value = NULL,
        .check_mask = NULL
    };
    jtag_add_dr_scan(tap, 1, &dr_field, TAP_IDLE);

    return jtag_execute_queue();
}
static int reset_dmi(struct jtag_tap *tap) {
    // 设置DMIRESET位(bit 16)
    uint32_t dtmcs_reset = (1 << 16);
    int retval = write_dtmcs(tap, dtmcs_reset);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to reset DMI");
        return retval;
    }
    
    // 清除DMIRESET位
    retval = write_dtmcs(tap, 0);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to clear DMIRESET");
        return retval;
    }
    
    return ERROR_OK;
}
static int read_dtmcs(struct jtag_tap *tap, uint32_t *dtmcs) {
    // 发送DTMCS IR指令(0x10)
    uint8_t dtmcs_ir = 0x10;
    struct scan_field ir_field = {
        .num_bits = tap->ir_length,
        .out_value = &dtmcs_ir,
        .in_value = NULL,
        .check_value = NULL,
        .check_mask = NULL
    };
    jtag_add_ir_scan(tap, &ir_field, TAP_IDLE);

    // 读取DTMCS DR值
    struct scan_field dr_field = {
        .num_bits = 32,
        .out_value = NULL,
        .in_value = (uint8_t *)dtmcs,
        .check_value = NULL,
        .check_mask = NULL
    };
    jtag_add_dr_scan(tap, 1, &dr_field, TAP_IDLE);

    return jtag_execute_queue();
}

static void analyze_dtmcs(uint32_t dtmcs) {
    // 按照精确位域解析
    bool dmihardreset = (dtmcs >> 17) & 0x1;
    bool dmireset = (dtmcs >> 16) & 0x1;
    uint8_t idle = (dtmcs >> 10) & 0x1F;  // 5 bits
    uint8_t dmistat = (dtmcs >> 8) & 0x3; // 2 bits
    uint8_t abits = (dtmcs >> 4) & 0xF;   // 4 bits
    uint8_t version = dtmcs & 0xF;        // 4 bits

    LOG_INFO("DTMCS Register: 0x%08x", dtmcs);
    LOG_INFO("  Version: 0x%x (%s)", version, 
            version == 1 ? "Debug 0.13" : "Unknown");
    LOG_INFO("  Address bits: %d", abits);
    LOG_INFO("  Idle cycles: %d", idle);
    LOG_INFO("  Status: %d (%s)", dmistat,
            dmistat == 0 ? "no error" : "error");
    LOG_INFO("  DMIRESET: %d, DMIHARDRESET: %d", 
            dmireset, dmihardreset);
}
// DMI操作类型
#define DMI_OP_NOP     0
#define DMI_OP_READ    1
#define DMI_OP_WRITE   2

// 常见的DM寄存器地址（可根据需要添加更多）
#define DM_DMCONTROL   0x10
#define DM_DMSTATUS    0x11
#define DM_HARTINFO    0x12
#define DM_HAWINDOWSEL    0x14
#define DM_HAWINDOW    0x15
#define DM_ABSTRACTCS  0x16
#define DM_COMMAND     0x17
#define DM_DATA0       0x04

// 正确的DMCONTROL位域定义
#define DMCONTROL_HALTREQ       (1 << 31)
#define DMCONTROL_RESUMEREQ     (1 << 30)
#define DMCONTROL_HARTRESET     (1 << 29)
#define DMCONTROL_ACKHAVERESET  (1 << 28)
#define DMCONTROL_HASEL         (1 << 26)
#define DMCONTROL_HARTSELLO(x)  (((x) & 0x3FF) << 16)
#define DMCONTROL_HARTSELHI(x)  (((x) & 0x3FF) << 6)
#define DMCONTROL_SETRESETHALTREQ (1 << 3)
#define DMCONTROL_CLRRESETHALTREQ (1 << 2)
#define DMCONTROL_NDMRESET      (1 << 1)
#define DMCONTROL_DMACTIVE      (1 << 0)

// 定义DMSTATUS位域
#define DMSTATUS_IMPEBREAK     (1 << 22)
#define DMSTATUS_ALLHAVERESET  (1 << 19)
#define DMSTATUS_ANYHAVERESET  (1 << 18)
#define DMSTATUS_ALLRESUMEACK  (1 << 17)
#define DMSTATUS_ANYRESUMEACK  (1 << 16)
#define DMSTATUS_ALLNONEXISTENT (1 << 15)
#define DMSTATUS_ANYNONEXISTENT (1 << 14)
#define DMSTATUS_ALLUNAVAIL    (1 << 13)
#define DMSTATUS_ANYUNAVAIL    (1 << 12)
#define DMSTATUS_ALLRUNNING    (1 << 11)
#define DMSTATUS_ANYRUNNING    (1 << 10)
#define DMSTATUS_ALLHALTED     (1 << 9)
#define DMSTATUS_ANYHALTED     (1 << 8)
#define DMSTATUS_AUTHENTICATED (1 << 7)
#define DMSTATUS_AUTHBUSY      (1 << 6)
#define DMSTATUS_HASRESETHALTRQ (1 << 5)
#define DMSTATUS_CONFSTRPTRVALID (1 << 4)
#define DMSTATUS_VERSION_SHIFT 0
#define DMSTATUS_VERSION_MASK  0xF


// hawindowsel
#define HAWINDOWSEL_hawindowsel(x)     (((x) & 0x7fff))

// hawondow
#define HAWINDOW_hawindow(x) (((x) & 0xffffffff))


// DMI访问函数
static int dmi_op(struct jtag_tap *tap, uint32_t addr, uint32_t *data, uint32_t op)
{
    // jtag_add_tlr();
    // 发送DMI IR指令(0x11)
    uint8_t dmi_ir = 0x11;
    struct scan_field ir_field = {
        .num_bits = tap->ir_length,
        .out_value = &dmi_ir,
        .in_value = NULL,
        .check_value = NULL,
        .check_mask = NULL
    };
    jtag_add_ir_scan(tap, &ir_field, TAP_IDLE);

    // 准备DMI请求数据
    uint8_t dmi_out[8] = {0};
    uint8_t dmi_in[8] = {0};
    
    // 构建DMI请求：[address(abits) | data(32) | op(2)]
    uint32_t abits = 7; // 根据DTMCS寄存器中的abits字段确定，这里假设为7
    uint64_t dmi_request = ((uint64_t)addr << 34) | ((uint64_t)*data << 2) | op;
    
    buf_set_u64(dmi_out, 0, abits + 34, dmi_request);

    struct scan_field dr_field = {
        .num_bits = abits + 34,  // address + data + op
        .out_value = dmi_out,
        .in_value = dmi_in,
        .check_value = NULL,
        .check_mask = NULL
    };
    
    jtag_add_dr_scan(tap, 1, &dr_field, TAP_IDLE);
    
    if (jtag_execute_queue() != ERROR_OK)
        return ERROR_FAIL;
    
    // 解析返回结果
    uint64_t response = buf_get_u64(dmi_in, 0, abits + 34);
    uint32_t status = response & 0x3;  // 获取op字段（状态）
    
    if (status == 0) {
        // 操作成功，如果是读操作，更新data
        if (op == DMI_OP_READ)
            *data = (response >> 2) & 0xFFFFFFFF;
        return ERROR_OK;
    } else if (status == 2 || status == 3) {
        // 错误状态，需要重置DMI
        LOG_ERROR("DMI operation failed with status %d", status);
        reset_dmi(tap);
        return ERROR_FAIL;
    }
    
    return ERROR_FAIL;
}

// 读取DM寄存器的便捷函数
static int __attribute__((optimize("O0"))) dm_read(struct jtag_tap *tap, uint32_t addr, uint32_t *value)
{
    uint32_t data = 0;
    int retval = dmi_op(tap, addr, &data, DMI_OP_READ);
    
    if (retval != ERROR_OK)
    {
        return retval; 
    }

    
    // 需要再次读取以获取上一次操作的结果
    retval = dmi_op(tap, addr, &data, DMI_OP_READ);
    if (retval != ERROR_OK)
    {
        return retval;
    }

    
    *value = data;
    return ERROR_OK;
}

// 写入DM寄存器的便捷函数
static int dm_write(struct jtag_tap *tap, uint32_t addr, uint32_t value)
{
    uint32_t data = value;
    int retval = dmi_op(tap, addr, &data, DMI_OP_WRITE);
    
    if (retval != ERROR_OK)
        return retval;
    
    // 执行一个NOP操作以确保写入完成
    data = 0;
    retval = dmi_op(tap, addr, &data, DMI_OP_NOP);
    
    return retval;
}
void log_time_with_info(const char* message) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    long seconds = tv.tv_sec % 60;
    long milliseconds = tv.tv_usec / 1000;
    printf("[%02ld:%02ld:%03ld] %s\n", seconds / 60, seconds % 60, milliseconds, message);
}
static int myNewCPU_poll(struct target *target)
{
    (void)target;
    return ERROR_OK;
}
static int myNewCPU_examine(struct target *target)
{
    log_time_with_info("in myNewCPU_examine");
    log_time_with_info("    Read DTMCS");
    struct jtag_tap *tap = target->tap;
    if (!tap) {
        LOG_ERROR("TAP not initialized!");
        return ERROR_FAIL;
    }
    // 重置JTAG状态机
    jtag_add_tlr();
    
    
    // 准备IR扫描
    uint8_t idcode_ir = 0x01;  // IDCODE指令码
    uint8_t ir_in[1] = {0};
    uint8_t check_value = 0x01;  // 期望捕获的值
    uint8_t check_mask = 0x1f;   // 有效位掩码

    struct scan_field ir_field = {
        .num_bits = tap->ir_length,
        .out_value = &idcode_ir,
        .in_value = ir_in,
        .check_value = &check_value,
        .check_mask = &check_mask
    };

    // 执行IR扫描
    jtag_add_ir_scan(tap, &ir_field, TAP_IDLE);
    
    // 准备DR扫描（读取IDCODE）
    uint32_t idcode = 0;
    struct scan_field dr_field = {
        .num_bits = 32,
        .out_value = NULL,  // 纯读取操作
        .in_value = (uint8_t *)&idcode,
        .check_value = NULL,
        .check_mask = NULL
    };

    jtag_add_dr_scan(tap, 1, &dr_field, TAP_IDLE);
    
    if (jtag_execute_queue() != ERROR_OK) {
        LOG_ERROR("JTAG scan failed");
        return ERROR_FAIL;
    }

    // 验证IR扫描结果
    if ((ir_in[0] & check_mask) != check_value) {
        LOG_ERROR("IR scan failed: got 0x%02x (expected 0x%02x under mask 0x%02x)",
                 ir_in[0], check_value, check_mask);
        return ERROR_FAIL;
    }

    uint32_t dtmcs;
    if (read_dtmcs(tap, &dtmcs) == ERROR_OK) {
        analyze_dtmcs(dtmcs);
        
        // 检查调试模块是否就绪
        if (((dtmcs >> 8) & 0x3) != 0) {
            LOG_WARNING("Debug module reports error state");
        }
    }
    LOG_INFO("Read IDCODE: 0x%08x", idcode);

    log_time_with_info("    reset(not Hard) DTM");
    reset_dmi(tap);

    uint32_t dmcontrol_a = DMCONTROL_DMACTIVE | DMCONTROL_NDMRESET;  // 激活DM并复位系统
    if (dm_write(tap, DM_DMCONTROL, dmcontrol_a) == ERROR_OK) {
        LOG_INFO("Successfully wrote DMCONTROL (activate and reset)");
    }
    
    // 清除复位信号，保持DM激活
    dmcontrol_a = DMCONTROL_DMACTIVE;
    if (dm_write(tap, DM_DMCONTROL, dmcontrol_a) == ERROR_OK) {
        LOG_INFO("Successfully cleared reset and kept DM active");
    }
    log_time_with_info("    select hart 1");
    myNewCPU_selectHart1(target);
    myNewCPU_hawindowsel(target);
    myNewCPU_hawindow(target);
    log_time_with_info("    Reading dmstatus registers");
    // 读取DMSTATUS寄存器
    uint32_t dmstatus;
    if (dm_read(tap, DM_DMSTATUS, &dmstatus) == ERROR_OK) {
        LOG_INFO("DMSTATUS: 0x%08x", dmstatus);
        
        // 版本信息
        uint8_t version = dmstatus & DMSTATUS_VERSION_MASK;
        LOG_INFO("  Version: %d", version);
        
        // 核心状态
        LOG_INFO("  All harts halted: %d", (dmstatus & DMSTATUS_ALLHALTED) ? 1 : 0);
        LOG_INFO("  Any hart halted: %d", (dmstatus & DMSTATUS_ANYHALTED) ? 1 : 0);
        LOG_INFO("  All harts running: %d", (dmstatus & DMSTATUS_ALLRUNNING) ? 1 : 0);
        LOG_INFO("  Any hart running: %d", (dmstatus & DMSTATUS_ANYRUNNING) ? 1 : 0);
        
        // 复位状态
        LOG_INFO("  All harts have reset: %d", (dmstatus & DMSTATUS_ALLHAVERESET) ? 1 : 0);
        LOG_INFO("  Any hart has reset: %d", (dmstatus & DMSTATUS_ANYHAVERESET) ? 1 : 0);
        
        // 恢复确认
        LOG_INFO("  All resume acknowledged: %d", (dmstatus & DMSTATUS_ALLRESUMEACK) ? 1 : 0);
        LOG_INFO("  Any resume acknowledged: %d", (dmstatus & DMSTATUS_ANYRESUMEACK) ? 1 : 0);
        
        // 可用性
        LOG_INFO("  All harts unavailable: %d", (dmstatus & DMSTATUS_ALLUNAVAIL) ? 1 : 0);
        LOG_INFO("  Any hart unavailable: %d", (dmstatus & DMSTATUS_ANYUNAVAIL) ? 1 : 0);
        LOG_INFO("  All harts nonexistent: %d", (dmstatus & DMSTATUS_ALLNONEXISTENT) ? 1 : 0);
        LOG_INFO("  Any hart nonexistent: %d", (dmstatus & DMSTATUS_ANYNONEXISTENT) ? 1 : 0);
        
        // 认证和其他状态
        LOG_INFO("  Authenticated: %d", (dmstatus & DMSTATUS_AUTHENTICATED) ? 1 : 0);
        LOG_INFO("  Authentication busy: %d", (dmstatus & DMSTATUS_AUTHBUSY) ? 1 : 0);
        LOG_INFO("  Has reset halt request: %d", (dmstatus & DMSTATUS_HASRESETHALTRQ) ? 1 : 0);
        LOG_INFO("  Impebreak: %d", (dmstatus & DMSTATUS_IMPEBREAK) ? 1 : 0);
    }

    log_time_with_info("    Reading dmcontrol registers");
    // 读取DMControl寄存器
    uint32_t dmcontrol;
    if (dm_read(tap, DM_DMCONTROL, &dmcontrol) == ERROR_OK) {
        analyze_dmcontrol(dmcontrol);
    }

    target->state = TARGET_RUNNING;
    log_time_with_info("out myNewCPU_examine");
    return ERROR_OK;
}
static int myNewCPU_selectHart1(struct target *target)
{
    LOG_INFO("in myNewCPU_selectHart1");
    struct jtag_tap *tap = target->tap;
    if (!tap) {
        LOG_ERROR("TAP not initialized!");
        return ERROR_FAIL;
    }

    // 读取当前DMCONTROL值
    uint32_t dmcontrol;
    int retval = dm_read(tap, DM_DMCONTROL, &dmcontrol);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to read DMCONTROL");
        return retval;
    }

    // 设置hartsel lo 为1
    dmcontrol |= DMCONTROL_HARTSELLO(1);
    retval = dm_write(tap, DM_DMCONTROL, dmcontrol);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to set 设置hartselo");
        return retval;
    }

    return ERROR_FAIL;
}
static int myNewCPU_hawindowsel(struct target *target)
{
    LOG_INFO("in myNewCPU_hawindowsel");
    struct jtag_tap *tap = target->tap;
    if (!tap) {
        LOG_ERROR("TAP not initialized!");
        return ERROR_FAIL;
    }

    // 读取当前hartwindowsel值
    uint32_t hartwindowsel;
    int retval = dm_read(tap, DM_HAWINDOWSEL, &hartwindowsel);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to read DM_HAWINDOWSEL");
        return retval;
    }

    // 设置hartsel lo 为1
    hartwindowsel |= HAWINDOWSEL_hawindowsel(1);
    retval = dm_write(tap, DM_HAWINDOWSEL, hartwindowsel);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to set HAWINDOWSEL_hawindowsel");
        return retval;
    }

    retval = dm_read(tap, DM_HAWINDOWSEL, &hartwindowsel);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to read DM_HAWINDOWSEL");
        return retval;
    }
    LOG_INFO("0x%x",retval);

    return ERROR_FAIL;
}

static int myNewCPU_hawindow(struct target *target)
{
    LOG_INFO("in myNewCPU_hawindow");
    struct jtag_tap *tap = target->tap;
    if (!tap) {
        LOG_ERROR("TAP not initialized!");
        return ERROR_FAIL;
    }

    // 读取当前hawindow值
    uint32_t hawindow;
    int retval = dm_read(tap, DM_HAWINDOW, &hawindow);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to read DM_HAWINDOW");
        return retval;
    }

    // 
    hawindow |= HAWINDOW_hawindow(0x3);
    retval = dm_write(tap, DM_HAWINDOW, hawindow);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to set DM_HAWINDOW");
        return retval;
    }

    retval = dm_read(tap, DM_HAWINDOW, &hawindow);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to read DM_HAWINDOW");
        return retval;
    }
    LOG_INFO("0x%x",retval);
    
    return ERROR_FAIL;
}

static int myNewCPU_halt(struct target *target)
{
    LOG_INFO("in myNewCPU_halt callback");
    struct jtag_tap *tap = target->tap;
    if (!tap) {
        LOG_ERROR("TAP not initialized!");
        return ERROR_FAIL;
    }

    // 读取当前DMCONTROL值
    uint32_t dmcontrol;
    int retval = dm_read(tap, DM_DMCONTROL, &dmcontrol);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to read DMCONTROL");
        return retval;
    }

    // 设置HALTREQ位，同时保持DMACTIVE
    dmcontrol |= DMCONTROL_HALTREQ | DMCONTROL_DMACTIVE;
    retval = dm_write(tap, DM_DMCONTROL, dmcontrol);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to set HALTREQ");
        return retval;
    }

    // 等待hart进入halted状态
    int timeout = 100;  // 设置超时计数
    while (timeout-- > 0) {
        uint32_t dmstatus;
        retval = dm_read(tap, DM_DMSTATUS, &dmstatus);
        if (retval != ERROR_OK) {
            LOG_ERROR("Failed to read DMSTATUS");
            return retval;
        }

        if (dmstatus & DMSTATUS_ALLHALTED) {
            LOG_INFO("Target halted");
            target->state = TARGET_HALTED; //must do this,
            return ERROR_OK;
        }

        alive_sleep(10);  // 等待10ms再次检查
    }

    LOG_ERROR("Timeout waiting for hart to halt");
    return ERROR_FAIL;
}

static int myNewCPU_resume(struct target *target, bool current, target_addr_t address, 
    bool handle_breakpoints, bool debug_execution)
{
/*
    我将帮助你实现myNewCPU_resume函数，这是处理器恢复执行的关键函数。根据RISC-V调试规范，实现resume功能需要清除dmcontrol寄存器的haltreq位并设置resumereq位来请求目标hart恢复执行，同时通过dmstatus寄存器监控hart的running状态。函数需要处理current和address参数来决定从哪里恢复执行，handle_breakpoints参数用于控制是否处理断点，debug_execution参数用于指示是否在调试模式下执行。我会在现有的myNewCPU_resume函数中添加相关代码，包括设置resume请求、等待hart进入running状态、更新target状态等关键步骤。

    根据RISC-V调试规范实现resume功能，需要清除haltreq位并设置resumereq位，然后等待hart进入running状态。
*/
    LOG_INFO("in myNewCPU_resume callback");
    struct jtag_tap *tap = target->tap;
    if (!tap) {
        LOG_ERROR("TAP not initialized!");
        return ERROR_FAIL;
    }

    // 如果需要从指定地址恢复执行
    if (!current) {
        // 这里需要实现设置PC的逻辑
        // 通过抽象命令接口设置PC寄存器
        // LOG_INFO("Resuming from address 0x%08", address);
        // TODO: 实现设置PC的具体逻辑
    }

    // 处理断点
    if (handle_breakpoints) {
        // 这里需要实现断点处理逻辑
        LOG_INFO("Handling breakpoints before resume");
        // TODO: 实现断点处理的具体逻辑
    }

    // 设置调试模式
    if (debug_execution) {
        LOG_INFO("Resuming in debug mode");
        // TODO: 实现调试模式设置的具体逻辑
    }

    // 读取当前DMCONTROL值
    uint32_t dmcontrol;
    int retval = dm_read(tap, DM_DMCONTROL, &dmcontrol);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to read DMCONTROL");
        return retval;
    }

    // 清除HALTREQ位，设置RESUMEREQ位，同时保持DMACTIVE
    dmcontrol &= ~DMCONTROL_HALTREQ;
    dmcontrol |= DMCONTROL_RESUMEREQ | DMCONTROL_DMACTIVE;
    retval = dm_write(tap, DM_DMCONTROL, dmcontrol);
    if (retval != ERROR_OK) {
        LOG_ERROR("Failed to set RESUMEREQ");
        return retval;
    }

    // 等待hart进入running状态
    int timeout = 100;  // 设置超时计数
    while (timeout-- > 0) {
        uint32_t dmstatus;
        retval = dm_read(tap, DM_DMSTATUS, &dmstatus);
        if (retval != ERROR_OK) {
            LOG_ERROR("Failed to read DMSTATUS");
            return retval;
        }

        if (dmstatus & DMSTATUS_ALLRUNNING) {
            LOG_INFO("Target resumed");
            target->state = TARGET_RUNNING;
            return ERROR_OK;
        }

        alive_sleep(10);  // 等待10ms再次检查
    }

    LOG_ERROR("Timeout waiting for hart to resume");
    return ERROR_FAIL;
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

// 分析并打印DMCONTROL寄存器的详细信息
static void analyze_dmcontrol(uint32_t dmcontrol) {
    LOG_INFO("DMCONTROL Register: 0x%08x", dmcontrol);
    
    // 解析各个位域
    bool haltreq = (dmcontrol >> 31) & 0x1;
    bool resumereq = (dmcontrol >> 30) & 0x1;
    bool hartreset = (dmcontrol >> 29) & 0x1;
    bool ackhavereset = (dmcontrol >> 28) & 0x1;
    bool hasel = (dmcontrol >> 26) & 0x1;
    uint16_t hartsello = (dmcontrol >> 16) & 0x3FF;
    uint16_t hartselhi = (dmcontrol >> 6) & 0x3FF;
    bool setresethaltreq = (dmcontrol >> 3) & 0x1;
    bool clrresethaltreq = (dmcontrol >> 2) & 0x1;
    bool ndmreset = (dmcontrol >> 1) & 0x1;
    bool dmactive = dmcontrol & 0x1;
    
    // 打印各个位域的状态
    LOG_INFO("  Hart Selection:");
    LOG_INFO("    HASEL: %d (Hart Array Selection: %s)", hasel,
             hasel ? "Multiple harts" : "Single hart");
    LOG_INFO("    HARTSEL: 0x%03x (hi: 0x%03x, lo: 0x%03x)",
             (hartselhi << 10) | hartsello, hartselhi, hartsello);
    
    LOG_INFO("  Control Flags:");
    LOG_INFO("    HALTREQ: %d (Halt Request)", haltreq);
    LOG_INFO("    RESUMEREQ: %d (Resume Request)", resumereq);
    LOG_INFO("    HARTRESET: %d (Hart Reset)", hartreset);
    LOG_INFO("    ACKHAVERESET: %d (Acknowledge Reset)", ackhavereset);
    
    LOG_INFO("  Reset Control:");
    LOG_INFO("    SETRESETHALTREQ: %d (Set Reset-Halt-Request)", setresethaltreq);
    LOG_INFO("    CLRRESETHALTREQ: %d (Clear Reset-Halt-Request)", clrresethaltreq);
    LOG_INFO("    NDMRESET: %d (Non-Debug-Module Reset)", ndmreset);
    
    LOG_INFO("  Debug Module Status:");
    LOG_INFO("    DMACTIVE: %d (Debug Module %s)", dmactive,
             dmactive ? "Active" : "Reset");
}
