# RT-Thread Debug Bridge

## 1. 介绍

*RT-Thread Debug Bridge* (以下简称*RDB*)是一个在 RT-Thread 上，基于 USB/TCP等可靠通信协议的远程调试桥。
可用于PC与运行有RT-Thread的设备进行可靠通信的应用层协议框架

### 1.1 目录结构

| 名称 | 说明 |
| ---- | ---- |
| docs  | 文档目录 |
| examples | 例子目录，并有相应的一些说明 |
| inc  | 头文件目录 |
| src  | 源代码目录 |

### 1.2 许可证

*RT-Thread Debug Bridge* 遵循 **GPLv2** 许可，详见 `LICENSE` 文件。

### 1.3 依赖

- 基本依赖USB Device,依赖DFS Posix libc
- 文件传输依赖dstr
- shell 依赖finsh组件
- 需要有足够的FD数量

## 2. 启用RDB

### 2.1 开启USB Device（需要对应BSP中含有USB驱动）

在ENV menuconfig中开启USB Device:

```bash
RT-Thread Components  --->
    Device Drivers  --->
        Using USB  --->
            -*- Using USB device
            (4096) usb thread stack size
            (0x0483) USB Vendor ID
            (0x0010) USB Product ID
            [ ]   Enable composite device
            Device type (Using custom class by register interface)  --->
```

其中 VID PID可以自行设置，但要求在Windows系统中从未枚举过。

VID建议使用芯片厂商ID 避免一些麻烦 [VID列表](http://www.linux-usb.org/usb.ids)

### 2.2 选中RDB-SRC 软件包

使用 RDB 需要在 RT-Thread 的包管理器中选择它，具体路径如下：

```bash
RT-Thread online packages  --->
    tools packages  --->
        [*] rdb:RT-Thread Debug Bridge package for rt-thread  --->
            --- rdb:RT-Thread Debug Bridge package for rt-thread
            [*]   Enable example for rdb push/pull (NEW)
            [ ]   Enable example for rdb shell (NEW)
                  Version (latest)  --->
```



然后让 RT-Thread 的包管理器自动更新，或者使用 `pkgs --update` 命令更新包到 BSP 中。

## 3. 注意事项
**各项示例功能的开启与关闭请按需配置。**

**没有开的功能是用不了的！！！**

**需要足够的FD数量 仅rdb需要fd数量为 (服务数+1)x4**

FD数量配置路径：

```bash
RT-Thread Components  --->
    Device virtual file system  --->
        (64)  The maximal number of opened files
```


在通信带宽满载的情况下同时使用多种功能会出现某一功能假死现象。在实时性要求较高的场所不建议使用大带宽功能，如文件传输。

## 4. 其他参考文档

- [快速上手指南](docs/quick-start.md)
- [rdb服务开发文档](docs/service.md)
- [更多文档](docs/README.md)
