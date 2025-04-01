TCL（Tool Command Language）是一种简洁灵活的脚本语言，OpenOCD 使用它作为核心配置和交互语言。它的语法简单但功能强大，支持变量、控制结构、过程（函数）、字符串操作等。下面详细介绍 TCL 的基本语法和 OpenOCD 中常用的关键字及用法。

1. TCL 基本语法
(1) 命令结构
TCL 是 基于命令 的语言，每条语句格式为：

tcl
复制
command arg1 arg2 arg3 ...
命令 和 参数 用 空格或换行 分隔。

引号 用于包含带空格的参数：

tcl
复制
echo "Hello, OpenOCD!"
(2) 变量
用 set 定义变量，$ 引用变量：

tcl
复制
set chip_name "stm32f103"
echo "Target chip: $chip_name"
变量可以是 字符串、数字、列表。

(3) 注释
单行注释：

tcl
复制
# 这是注释
多行注释（通过 if 0 实现）：

tcl
复制
if 0 {
    这是多行注释
    不会被执行
}
2. TCL 支持的关键字（核心语法）
关键字/命令	说明	示例
set	定义变量	set x 10
if / elseif / else	条件判断	if {$x > 0} { echo "Positive" }
for	循环	for {set i 0} {$i < 5} {incr i} { echo $i }
foreach	遍历列表	foreach i {1 2 3} { echo $i }
proc	定义函数	proc add {a b} { return [expr $a + $b] }
return	返回值	return $result
expr	数学运算	set sum [expr 1 + 2]
puts / echo	打印输出	puts "Hello"
source	加载脚本	source "config.cfg"
array	数组操作	set arr(0) 100
list	列表操作	set mylist [list 1 2 3]
string	字符串操作	string length "OpenOCD"
3. OpenOCD 特有的 TCL 命令
OpenOCD 在标准 TCL 基础上扩展了 硬件调试相关命令：

(1) 调试控制
命令	说明
reset	复位目标芯片
halt	暂停 CPU
resume	继续运行
step	单步执行
reg	读写寄存器
(2) 内存访问
命令	说明
mdw 0x20000000	读取 32 位内存
mww 0x20000000 0x1234	写入 32 位内存
dump_image file.bin 0x20000000 1024	导出内存到文件
(3) Flash 编程
命令	说明
flash write_image firmware.bin 0x08000000	烧录固件
flash erase_sector 0 0 1	擦除 Flash 扇区
(4) 断点管理
命令	说明
bp 0x08000100 4 hw	设置硬件断点
rbp 0x08000100	删除断点
4. TCL 在 OpenOCD 中的典型用法
(1) 条件判断
tcl
复制
if {[target_name] == "stm32f103"} {
    echo "STM32F103 detected!"
} else {
    echo "Unsupported target."
}
(2) 循环遍历
tcl
复制
for {set i 0} {$i < 5} {incr i} {
    echo "Count: $i"
}
(3) 自定义函数
tcl
复制
proc read_reg {reg_addr} {
    set value [mrw $reg_addr]
    echo "Register $reg_addr = $value"
    return $value
}

read_reg 0x20000000
(4) 加载配置文件
tcl
复制
source [find interface/stlink.cfg]  # 加载 ST-Link 配置
source [find target/stm32f4x.cfg]   # 加载 STM32F4 配置
5. 注意事项
大括号 {} 和方括号 [] 的区别：

{} 用于 代码块 或 防止变量替换。

[] 用于 命令替换（类似 Shell 的反引号）。

tcl
复制
set cmd "echo"
{$cmd "Hello"}  # 输出: $cmd "Hello"（不执行替换）
[$cmd "Hello"]  # 输出: Hello（执行命令）
变量替换时机：

TCL 在 解析阶段 替换变量，可能导致意外行为：

tcl
复制
set x 10
if {$x > 0} { ... }  # 正确
if "$x > 0" { ... }  # 可能报错（引号会提前展开）
OpenOCD 扩展命令：

OpenOCD 的 find、target、flash 等命令是 OpenOCD 特有，不是标准 TCL。

6. 总结
TCL 是 OpenOCD 的“大脑”，用于配置硬件、控制调试流程。

语法简单：set、if、for、proc 等关键字易学易用。

OpenOCD 扩展：支持 reset、flash、bp 等硬件操作命令。

适合嵌入式调试：通过 TCL 脚本实现自动化烧录、调试。