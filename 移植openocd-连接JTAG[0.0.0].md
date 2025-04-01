# 怎么算OpenOCD连接上了目标芯片JTAG口呢
- 读出目标芯片的IDCODE




## TCL是什么？
- 全称Tool command language,是一种脚本语言，用这种语言可以控制openocd的行为

## 那它是一种脚本语言，那它的语法，支持哪些关键字呢？
- 语法、变量、关键字、函数、也就是说它也能做条件判断、循环、函数调用的一些事情
- openocd在TCL扩展出来其它的一些命令关键字

## 连接上JTAG的最小配置

# 创建TAP
```TCL
adapter driver ftdi 
adapter speed 4000
transport select jtag

#0.12.0
#assign vid pid
ftdi vid_pid 0x0403 0x6010
<!-- 方向掩码 0x0008：
二进制 0000 0000 0000 1000，表示 ?TMS 引脚（bit3）为输出?（其他 GPIO 默认为输入）。
?数据掩码 0x001b：
二进制 0000 0000 0001 1011，初始化时设置 ?TCK=1、TDI=1、TMS=1?（JTAG 默认高电平）。 -->
ftdi layout_init 0x0008 0x001b
ftdi layout_signal nSRST -oe 0x0020 -data 0x0020  # nSRST 信号（复位）绑定到 GPIO5（bit5）
ftdi layout_signal TCK -data 0x0001               # TCK 绑定到 GPIO0（bit0）
ftdi layout_signal TDI -data 0x0002               # TDI 绑定到 GPIO1（bit1）
ftdi layout_signal TDO -input 0x0004              # TDO 绑定到 GPIO2（bit2，输入模式）
ftdi layout_signal TMS -data 0x0008               # TMS 绑定到 GPIO3（bit3）
adapter speed 4000

jtag newtap S3 myNewCPU -irlen 5
target create S3.myNewCPU myNewCPU -chain-position S3.myNewCPU
```


## 修改了哪些源码部分？
- target_types 新增一个我的target type
- flash_drivers 新增一个我的flashdriver
- makefile