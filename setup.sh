#!/bin/bash
set -e

echo "Enabling I2C, SPI, and Console Auto login..."
sudo raspi-config nonint do_i2c 0 || { echo "Error: Failed to enable I2C."; exit 1; }
sudo raspi-config nonint do_spi 0 || { echo "Error: Failed to enable SPI."; exit 1; }
sudo raspi-config nonint do_boot_behaviour B2 || { echo "Error: Failed to enable Console Auto login."; exit 1; }

echo "Updating and installing dependencies..."
sudo apt-get -y install git raspberrypi-kernel-headers < "/dev/null" || { echo "Error: Failed to install dependencies."; exit 1; }

echo "Customising ~/.bashrc..."
sudo cp ~/beepberry_setup/misc/.bashrc ~/

echo "Downloading Beepberry software..."
git clone https://github.com/TheMediocritist/beepberry_setup

echo "Compiling and installing display driver..."
cd ~/beepberry_setup/display/
make || { echo "Error: Failed to compile display driver."; exit 1; }
sudo make modules_install || { echo "Error: Failed to install display driver."; exit 1; }
sudo depmod -A || { echo "Error: Failed to update module dependencies."; exit 1; }
echo 'sharp' | sudo tee -a /etc/modules
dtc -@ -I dts -O dtb -o sharp.dtbo sharp.dts || { echo "Error: Failed to compile device tree."; exit 1; }
sudo cp sharp.dtbo /boot/overlays
echo -e "framebuffer_width=400\nframebuffer_height=240\ndtoverlay=sharp" | sudo tee -a /boot/config.txt
sudo sed -i ' 1 s/.*/& fbcon=map:10 fbcon=font:VGA8x16/' /boot/cmdline.txt || { echo "Error: Failed to modify cmdline.txt."; exit 1; }

# No keyboard for now
# echo "Compiling and installing keyboard device driver..."
# cd ~/
# git clone https://github.com/w4ilun/bbqX0kbd_driver.git || { echo "Error: Failed to clone keyboard driver repository."; exit 1; }
# cd ~/beepberry_setup/keyboard
# ./installer.sh --BBQ20KBD_TRACKPAD_USE BBQ20KBD_TRACKPAD_AS_KEYS --BBQX0KBD_INT BBQX0KBD_USE_INT || { echo "Error: Failed to install keyboard device driver."; exit 1; }

echo "Connecting to i4 bluetooth keyboard..."
# echo "connect AA:BB:CC:DD:EE:FF \nquit" | bluetoothctl

echo "Installing raspi2fb..."
sudo apt-get -y install cmake 
sudo apt-get -y install libbsd-dev
cd ~/beepberry_setup/raspi2fb/
mkdir build
cd build
cmake ..
make
sudo make install
sudo cp ../raspi2fb.init.d /etc/init.d/raspi2fb
sudo update-rc.d raspi2fb defaults

echo "Rebooting..."
sudo shutdown -r now || { echo "Error: Failed to reboot."; exit 1; }