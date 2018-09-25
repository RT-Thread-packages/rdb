# 快速上手指南

## 1. sample1 - rdb shell

### 1.1 简介

rdb shell是一个用于finsh/msh通信的rdb服务

### 1.2 RT-Thread端

#### 1.2.1 开启rdb shell

在env中配置开启rdb并启用rdb shell的示例

```bash
RT-Thread online packages  --->
    tools packages  --->
        [*] rdb:RT-Thread Debug Bridge package for rt-thread  --->
            --- rdb:RT-Thread Debug Bridge package for rt-thread
            [ ]   Enable example for rdb push/pull (NEW)
            [*]   Enable example for rdb shell (NEW)
                  Version (latest)  --->
```

#### 1.2.2 编译并下载工程

由于bsp情况复杂 此处不再多做具体bsp的编译烧录说明

### 1.3 通过USB连接PC与设备

### 1.4 PC端

#### 1.4.1 请确认PC环境

- Windows7 SP1 或更高版本
- 已安装.NET Framework 4.6.1 或更高版本
- 若为Windows 7 请安装对应驱动 驱动中的设备路径需要修改，请参阅驱动下的readme.md文件
- Windows 8 或更高版本的系统不需要手动安装驱动，若无法正常识别，请确认硬件问题或考虑安装MSDN版本的Windows系统

#### 1.4.3 启动shell控制台

在env 中执行

```bash
rdb list
rdb connect 0
rdb shell
```

此时rdb会启动一个telent的putty客户端 并自动连接上设备

其中 rdb connect 0中的0为设备序号 当PC端接入了多个rdb设备时 通过设备号区分.

在rdb connect成功之后，不需要运行多次 connect命令

#### 1.4.4 杀死 rdb服务

当rdb服务异常时 使用如下命令杀死 rdb服务

```bash
rdb kill
```

## 2 sample2 - rdb push/pull

rdb push/pull 是一个用于文件传输的rdb服务

使用流程与rdb shell相同此处不再赘述

### 2.1 相关命令如下

```bash
rdb push LOCALPATH REMOTEPATH
rdb pull REMOTEPATH [LOCALPATH]
```

LOCALPATH 为本地路径 可以为目录也可以为文件
REMOTEPATH 为设备端路径 可以为目录也可以为文件

### 2.2 注意事项

当传输大文件时会占满带宽，造成其他服务假死。