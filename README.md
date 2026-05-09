# J-Link RTT S32K3 数据可视化示例

本项目演示了如何利用 SEGGER J-Link 的 RTT（Real Time Transfer）技术，从 NXP S32K3 系列微控制器实时导出数据，并使用 VOFA+ 上位机软件进行图形化展示。

## 项目组成

- **SEGGER_RTT 核心文件**:
    - [SEGGER_RTT.c](SEGGER_RTT.c) / [SEGGER_RTT.h](SEGGER_RTT.h): 从 SEGGER 官方库移植并针对 S32K3 进行了精简，仅保留了必要的初始化和二进制数据发送功能。
    - [SEGGER_RTT_Conf.h](SEGGER_RTT_Conf.h): RTT 配置头文件。
- **[main.c](main.c)**: S32K314 端的示例代码。
    - 在循环中计算 `sin` 函数值。
    - 使用 `SEGGER_RTT_Write` 将数据（float 数组）写入 RTT 缓冲区。
    - 为了配合 VOFA+ 的 `JustFloat` 协议，数据包中包含了一个特殊的结束符（Infinity）。
- **[start.py](start.py)**: 主机端 Python 辅助脚本。
    - 负责连接本地 19021 端口（J-Link RTT TELNET 端口）。
    - 发送符合 SEGGER 协议的控制命令字 `$$SEGGER_TELNET_ConfigStr=...$$`，用于告知调试器 RTT 控制块（CB）的内存地址。
- **VOFA+**: 第三方数据可视化软件，用于接收并绘制波形。

## 工作原理

1.  **RTT 机制**: RTT 是 SEGGER 推出的一种在嵌入式目标和主机之间进行高速数据传输的技术。它通过调试接口（如 SWD/JTAG）直接读写内存中的环形缓冲区，不占用目标 CPU 的串口等外设。
2.  **数据流向**:
    - **S32K3**: 运行 `main.c`，将计算好的波形数据写入指定的 RTT 缓冲区。
    - **J-Link GDB Server**: 在 S32DS 调试过程中，GDB Server 会监听本地 TCP 19021 端口。
    - **start.py**: 脚本连接 19021 端口并发送配置字符串（包含 `SetRTTAddr` 命令）。这一步非常关键，因为它显式指定了 RTT 控制块在 S32K3 内存中的地址，确保调试器能正确找到数据。
    - **VOFA+**: 选择 `JustFloat` 协议，连接到 19021 端口，接收来自 J-Link 的原始数据流并解析成波形。

## 使用步骤

1.  **编译与调试**: 使用 S32DS 编译 [main.c](main.c) 并启动调试（Debug）。确保 J-Link GDB Server 正在运行。
2.  **获取控制块地址**:
    - 在编译生成的 `.map` 文件中搜索 `_SEGGER_RTT` 符号，找到其内存地址。
    - **TCM 地址转换**: 对于 S32K3，如果 `_SEGGER_RTT` 位于 TCM（如 `0x2000....` 或 `0x0000....`），必须将其转换为 **Backdoor 地址** 供 J-Link 使用。
        - DTCM (0x2000xxxx) -> Backdoor (0x2040xxxx)
        - ITCM (0x0000xxxx) -> Backdoor (0x0040xxxx)
    - 例如：`.map` 中显示 `0x20000080`，则实际使用的地址为 `0x20400080`。
3.  **触发 RTT**: 运行 [start.py](start.py) 脚本，并传入转换后的地址。
    ```bash
    python3 start.py 0x20400080
    ```
4.  **可视化展示**:
    - 打开 [VOFA+](https://www.vofa.plus/)。
    - 在设置中选择 `TCP Client` 连接 `127.0.0.1:19021`。
    - 协议选择 `JustFloat`。
    - 即可看到实时的正弦波形输出。

    ![VOFA+ 截图](vofa+.png)

## 注意事项

- **地址配置**: [start.py](start.py) 需要正确的 RTT 控制块地址。如果重新编译后地址发生变化，必须更新命令行参数。
- **协议兼容**: 本项目使用了简单的二进制流传输。VOFA+ 的 `JustFloat` 协议要求数据包以 `0x00 0x00 0x80 0x7f` (float 的 Infinity) 结尾作为帧分隔符。
