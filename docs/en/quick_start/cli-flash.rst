####################
Command Line Flashing Guide
####################

Firmware Flashing Module
========================

1. On Linux systems, first set serial port permissions with the following command to avoid errors:

.. code-block:: bash

    sudo usermod -aG dialout $USER

.. note:: System reboot required after configuration.

2. Run ``tos flash`` command in compiled project directory for one-click flashing. The command automatically downloads tyutool and performs flashing:

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
    Chip must enter boot mode before flashing.
    If serial port is unresponsive, check port selection or potential conflicts.

3. tos flash tool keeps adding support for new chips. Versions before v1.8.0 require manual upgrades:

Check version::

    tos flash --version

Example output:

.. code-block:: bash

    tyuTool, version 1.8.3

For versions prior to v1.8.0, manually upgrade with:

.. code-block:: bash

    tos flash upgrade

Authorization Code Flashing
===========================
Use ``tos monitor`` command to select the log serial port and view device logs in the terminal.
Use ``tos monitor -b 115200`` to select the flashing serial port and perform authorization code burning via serial command line.

.. code-block:: bash
    cd apps/tuya_cloud/switch_demo
    tos monitor -b 115200
    tyutool params:

.. list-table::
   :header-rows: 1

   * - Command
     - Description
   * - ``hello``
     - Test command line functionality, return ``hello world``
   * - ``auth``
     - Initiate authorization code flashing
   * - ``auth-read``
     - Read authorization code

Send commands via serial console for authorization:

.. code-block:: bash

   auth uuidxxxxxxxxxxxxxxxx keyxxxxxxxxxxxxxxxxxxxxxxxxxxxxx

.. note:: Press Enter after typing command before clicking Send button.