########################
GUI Flashing Tool Guide
########################

************************
Windows GUI Environment
************************

.. image:: https://images.tuyacn.com/fe-static/docs/img/dc641b75-663b-4341-8d22-56bd9e83a718.png

Firmware Flashing Module
=========================

**Operation Process:**

1. Firmware Selection
    - Navigate to ``.build/bin`` directory and select target firmware (recommended to use ``QIO.bin``)

2. Parameter Configuration
    - Port scan: Auto-detect available COM ports
    - Baud rate: Default 921600 bps
    - Chip platform: Select module model from top-right dropdown

3. Execute Flashing
    - Click ``Start`` button to begin flashing process, progress bar shows real-time status

**Flashing Log Output:**

.. code-block:: c

    [INFO]: Write Start.
    [INFO]: Waiting Reset ...
    [INFO]: unprotect flash OK.
    [INFO]: sync baudrate 921600 success
    [INFO]: Erase flash success
    [INFO]: Write flash success
    [INFO]: CRC check success
    [INFO]: Reboot done

Serial Port Interface
=====================

Provides serial communication debugging solution with real-time data interaction and authorization management.

.. image:: https://images.tuyacn.com/fe-static/docs/img/5f41aab9-b3c4-4ece-abca-1223503f7a4e.png

Functional Components
---------------------

1. Communication Configuration
    - Click ``COM`` for automatic port scanning
    - Select target device's COM port identifier
    - Maintain default settings (Data bits:8, Stop bits:1, No parity)

2. Session Management
    - Start debugging: Click ``Start`` to establish connection
    - Terminate session: Click ``Stop`` to safely close connection
    - Session history: Save recent session logs

3. Data Operations
    - Send window: Supports HEX/ASCII dual-mode input
    - Receive window: Real-time communication data display

Authorization Code Burning
--------------------------

1. Click ``Authorize`` to activate device authentication mode
2. Input in security dialog:
    - **Device UUID**: 20-digit unique identifier
    - **Secure Key**: 32-digit encryption key
3. Authentication result:
    - Success prompt: ``Authorization write succeeds.``