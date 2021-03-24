###########################################################

Time-of-Flight (ToF) Linux Reference Driver README.md

###########################################################

1. Description

    Contains the release files for adding tmf8701 support to the linux kernel

    /arch/arm/boot/dts
        Contains the device tree overlay for the tmf8701 proto-
        typed on the Raspberry Pi zero. The rpi.env script is a convenience
        script for setting up build environment variables to point to a remote
        linux kernel source tree. This prototyped version uses GPIO_20 for INT
        and GPIO_16 for the "Chip-Enable" line

        -tof8701-overlay.dts
          normal configuration, CE gpio and INT gpio
        -tof8701-overlay-polled.dts
          Polled I/O configuration, CE gpio and polled interrupt
        -tof8701-overlay-polled-nogpio.dts
          Fully-polled configuration, no gpios all I/O through polling


2. Compiling

    Driver - Compiling

       1. Download Linux Kernel source:

       2. Compile Linux Kernel

       4. Compile ToF Device Tree

       5. Compile ToF driver:
            - 'make CONFIG_SENSORS_TMF8701=m'
