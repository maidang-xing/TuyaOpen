#############
命令行烧录指南
#############

固件烧录模块
============

1. 在 Linux 环境下需要先使用如下命令设置串口权限，否则运行会报错：

.. code-block:: bash

    sudo usermod -aG dialout $USER

.. note:: 设置完成后需重启系统方可生效。

2. 在需要编译完成后的项目中运行 ``tos flash`` 命令一键烧录，``tos flash`` 会根据当前运行的环境自动下载对应的 tyutool 工具，并自动烧录：

.. code-block:: bash

    cd apps/tuya_cloud/switch_demo
    tos flash
    tyutool params:
    [INFO]: tyut_logger init done.
    [INFO]: Run Tuya Uart Tool.
    [INFO]: Use default baudrate: [921600]
    [INFO]: Use default start address: [0x00]
    --------------------
    1. /dev/ttyS0
    2. /dev/ttyACM0
    3. /dev/ttyACM1
    ^^^^^^^^^^^^^^^^^^^^
    Select serial port: 3
    [INFO]: Waiting Reset ...
    [INFO]: unprotect flash OK.
    [INFO]: sync baudrate 921600 success
    Erasing: ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ 100% 4 bytes/s   0:00:04 / 0:00:00
    [INFO]: Erase flash success
    Writing: ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ 100% 16 bytes/s   0:00:18 / 0:00:00
    [INFO]: Write flash success
    [INFO]: CRC check success
    [INFO]: Reboot done
    [INFO]: Flash write success.

.. attention::
    烧录过程中需要根据芯片实际情况进入 boot 后才可以进行串口烧录。
    烧录过程中如果串口没有响应，请检查串口是否正确选择，或串口是否被其他程序占用。

3. tos flash 烧录工具正在不断新增支持新的芯片型号，v1.8.0 之前的版本不支持自动升级工具，后续版本在启动时会检测升级并提示升级。
可通过::

    tos flash --version

查询版本情况：

.. code-block:: bash

    tyuTool, version 1.8.3

v1.8.0 之前版本需要手工运行以下升级命令升级：

.. code-block:: bash

    tos flash upgrade

授权码烧录
==========
使用 ``tos monitor`` 命令，选择日志串口，在终端查看设备日志信息。
使用 ``tos monitor -b 115200`` ,选择烧录串口，使用串口命令行进行授权码烧录。

.. code-block:: bash
    cd apps/tuya_cloud/switch_demo
    tos monitor -b 115200
    tyutool params:

.. list-table::
   :header-rows: 1

   * - 命令
     - 说明
   * - ``hello``
     - 测试命令行功能，返回 ``hello world``
   * - ``auth``
     - 提示烧录授权码操作
   * - ``auth-read``
     - 读取授权码

发送窗口使用命令进行授权码烧录。

.. code-block:: bash

   auth uuidxxxxxxxxxxxxxxxx keyxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

.. note:: 使用发送窗口发送命令行需要在命令末尾输入回车再点击发送。
