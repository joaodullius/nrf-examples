=================================
nRF-Examples by Joao Dullius
=================================

nRF-Examples is a repository containing sample code that demonstrates various functionalities of the nRF9160 development board. The examples cover a range of topics, including low-power modes, wireless communication, and hardware peripherals.

=================================
Examples
=================================

* **disable_uart_runtime** - This example demonstrates how to disable UARTs during runtime to conserve power. By disabling the UARTs when they are not in use, this can significantly reduce the power consumption of the device, making it ideal for battery-powered devices.

* **gnss_coap** - This example showcases how to obtain a GPS fix and send the data to a CoAP server using the nRF9160 modem. This can be used in location-based applications where the device needs to communicate its position to a server. It demonstrates how to configure the modem, set up a GNSS receiver, and communicate with a CoAP server.

* **hello_world_rtt** - This is a simple "Hello World" example that demonstrates the use of the Segger Real-Time Transfer (RTT) interface for console output. The RTT interface allows for efficient debugging and logging without the need for a physical UART connection. It demonstrates how to configure the RTT interface and how to use it for console output.

* **i2c_scanner** - This example demonstrates how to scan the I2C bus for connected devices. It can be used to detect and troubleshoot I2C communication issues in embedded systems. It demonstrates how to configure the I2C interface and how to scan the bus for connected devices.

* **lp_hc_nrf9160dk** - This example shows how to run a Bluetooth Low Energy (BLE) Host on a nRF9160 and a BLE Controller on a nRF523, interfacing through hci_lpuart. It demonstrates how to set up a BLE connection and communicate between the devices using HCI over UART.

* **rtc_sleep** - This example demonstrates a simple Real-Time Counter (RTC) timer that toggles an LED. This can be used to measure the current baseline power consumption of the device. It demonstrates how to configure the RTC timer and how to toggle an LED using the General-Purpose Input/Output (GPIO) interface.

* **rtc_sleep_dual_uart** - This is the same as the above example, but it disables UART0 and UART1 during runtime. This can reduce the power consumption of the device when the UARTs are not in use.

* **simple_service** - This example demonstrates how to implement a custom BLE service on both the peripheral (advertising and GATT definition) and server side (discovery and connection). It also implements simple fixed password pairing and bonding. It demonstrates how to set up a custom BLE service and how to handle pairing and bonding between devices.

* **timer_gppi_gpiote** - This example implements a hardware timer that is connected by either PPI or DPPI to a GPIOTE to blink an LED. It demonstrates how to configure the timer and how to use the Programmable Peripheral Interconnect (PPI) or Direct Peripheral-to-Peripheral Interconnect (DPPI) interfaces to trigger a GPIOTE event.

* **uart_wakeup_rx** - This example demonstrates how to wake up the nRF9160 from sleep using UART activity on RX. It uses low-power states and enables UART only when needed to reduce current consumption. Useful for applications waiting for external commands or serial triggers without wasting energy in idle mode.

=================================
Other Examples
=================================

There are also other examples in the repository not listed here, which are older ones that are pending review.
