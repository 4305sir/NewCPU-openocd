#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include "myNewCPU_flash.h"
#include <target/target.h>

struct myNewCPU_flash_bank {
    /* 芯片特定的 flash 参数 */
    int variant;
    uint32_t base_address;
    int flash_size;
    int page_size;
    /* 可能的工作区算法数据 */
    struct working_area *write_algorithm;
};

/* Flash 算法二进制数据（如果使用目标端算法） */
static const uint8_t myNewCPU_flash_write_code[] = {
    /* 此处是预编译的目标芯片机器码 */
    0x01, 0x02, 0x03, 0x04, /* 示例数据 */
};

/* Flash 驱动命令处理函数 */
FLASH_BANK_COMMAND_HANDLER(myNewCPU_flash_bank_command)
{
    struct myNewCPU_flash_bank *myNewCPU_info;
    
    myNewCPU_info = malloc(sizeof(struct myNewCPU_flash_bank));
    if (!myNewCPU_info)
        return ERROR_FAIL;
        
    bank->driver_priv = myNewCPU_info;
    /* 初始化 flash 信息... */
    (void)myNewCPU_flash_write_code;
    return ERROR_OK;
}

/* 实现擦除功能 */
static int myNewCPU_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
    struct myNewCPU_flash_bank *myNewCPU_info = bank->driver_priv;
    struct target *target = bank->target;
    
    /* 实现擦除逻辑... */
    (void)target;
    (void)myNewCPU_info;

    return ERROR_OK;
}

/* 实现写入功能 */
static int myNewCPU_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
    struct myNewCPU_flash_bank *myNewCPU_info = bank->driver_priv;
    struct target *target = bank->target;
    
    /* 实现写入逻辑，可以使用主机端或目标端算法 */
    (void)target;
    (void)myNewCPU_info;

    return ERROR_OK;
}

/* 实现探测功能 */
static int myNewCPU_probe(struct flash_bank *bank)
{
    struct myNewCPU_flash_bank *myNewCPU_info = bank->driver_priv;
    struct target *target = bank->target;
    
    /* 探测 flash 参数，设置扇区信息等... */
    (void)target;
    (void)myNewCPU_info;

    return ERROR_OK;
}

/* 定义 flash 驱动 */
struct flash_driver myNewCPU_flash = {
    .name = "myNewCPU",
    .usage = "flash bank <name> myNewCPU <base> <size> 0 0 <target>",
    // .commands = myNewCPU_command_handlers,
    .commands = NULL,
    .flash_bank_command = myNewCPU_flash_bank_command,
    .erase = myNewCPU_erase,
    .protect = NULL,
    .write = myNewCPU_write,
    .read = default_flash_read,
    .probe = myNewCPU_probe,
    .auto_probe = myNewCPU_probe,
    .erase_check = default_flash_blank_check,
    .protect_check = NULL,
    // .info = myNewCPU_get_info,
    .info = NULL,
};