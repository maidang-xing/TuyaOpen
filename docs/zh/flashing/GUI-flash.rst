################
GUI 烧录工具指南
################

************
win 环境 GUI
************

.. image:: https://images.tuyacn.com/fe-static/docs/img/dc641b75-663b-4341-8d22-56bd9e83a718.png

固件烧录模块
============

**操作流程：**

1. 固件选择
    - 导航至 ``.build/bin`` 目录，选择目标固件文件（推荐使用 ``QIO.bin``）

2. 参数配置
    - 端口扫描：自动检测可用 COM 端口
    - 波特率设置：默认 921600 bps（默认值）
    - 芯片平台：从右上角下拉菜单选择对应模组型号

3. 执行烧录
    - 点击 ``Start`` 按钮启动烧录流程，进度条实时显示传输状态

**烧录日志输出：**

.. code-block:: c

    [INFO]: Write Start.
    [INFO]: Waiting Reset ...
    [INFO]: unprotect flash OK.
    [INFO]: sync baudrate 921600 success
    [INFO]: Erase flash success
    [INFO]: Write flash success
    [INFO]: CRC check success
    [INFO]: Reboot done

串口页面
========

提供串行通信调试，支持多平台开发板的实时数据交互与授权管理功能。

.. image:: https://images.tuyacn.com/fe-static/docs/img/5f41aab9-b3c4-4ece-abca-1223503f7a4e.png

功能组成
--------
1. 通信参数配置
    - 单击 ``COM`` 按钮执行自动端口扫描
    - 选择目标设备对应的COM端口标识
    - 保持默认配置（数据位：8，停止位：1，无校验）

2. 会话管理
    - 启动调试：单击 ``Start`` 建立通信会话
    - 会话终止：单击 ``Stop`` 安全关闭连接
    - 会话记录：保存最近会话日志

3. 数据操作
    - 发送窗口：支持HEX/ASCII双模式输入
    - 接收窗口：实时显示通信数据

授权码烧录
----------

1. 单击 ``Authorize`` 激活设备认证模式
2. 在安全认证对话框中录入：
    - **Device UUID**：20位设备唯一标识符
    - **Secure Key**：32位加密认证密钥
3. 认证结果：成功提示： ``Authorization write succeeds.``
