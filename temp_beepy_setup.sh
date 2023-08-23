#!/bin/bash
set -e

echo "Enabling I2C, SPI, and Console Auto login..."
sudo raspi-config nonint do_i2c 0 || { echo "Error: Failed to enable I2C."; exit 1; }
sudo raspi-config nonint do_spi 0 || { echo "Error: Failed to enable SPI."; exit 1; }
sudo raspi-config nonint do_boot_behaviour B2 || { echo "Error: Failed to enable Console Auto login."; exit 1; }

echo "Updating and installing dependencies..."
sudo apt-get -y install git raspberrypi-kernel-headers < "/dev/null" || { echo "Error: Failed to install dependencies."; exit 1; }

echo "Compiling and installing display driver..."
cd ~/
git clone https://github.com/ardangelo/sharp-drm-driver
cd sharp-drm-driver
make
sudo make install
sudo sed -i 's/8x8/8x16/' /boot/cmdline.txt

echo "Compiling and installing keyboard device driver..."
cd ~/
git clone https://github.com/sqfmi/bbqX0kbd_driver.git || { echo "Error: Failed to clone keyboard driver repository."; exit 1; }
cd ~/bbqX0kbd_driver
echo "Reverting to June 12 commit..."
git reset --hard 802b73f

./installer.sh --BBQ20KBD_TRACKPAD_USE BBQ20KBD_TRACKPAD_AS_KEYS --BBQX0KBD_INT BBQX0KBD_USE_INT || { echo "Error: Failed to install keyboard device driver."; exit 1; }

echo "Rebooting..."
sudo shutdown -r now || { echo "Error: Failed to reboot."; exit 1; }
