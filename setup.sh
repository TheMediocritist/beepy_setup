#!/bin/bash
set -e

echo "Enabling I2C, SPI, and Console Auto login..."
sudo raspi-config nonint do_i2c 0 || { echo "Error: Failed to enable I2C."; exit 1; }
sudo raspi-config nonint do_spi 0 || { echo "Error: Failed to enable SPI."; exit 1; }
sudo raspi-config nonint do_boot_behaviour B2 || { echo "Error: Failed to enable Console Auto login."; exit 1; }

echo "Updating and installing dependencies..."
sudo apt-get -y install git raspberrypi-kernel-headers < "/dev/null" || { echo "Error: Failed to install dependencies."; exit 1; }

echo "Downloading Beepberry software..."
git clone https://github.com/TheMediocritist/beepberry_setup

echo "Customising ~/.bashrc..."
sudo cp ~/beepberry_setup/misc/.bashrc ~/

echo "Compiling and installing display driver..."
cd ~/beepberry_setup/display/
make || { echo "Error: Failed to compile display driver."; exit 1; }
sudo make modules_install || { echo "Error: Failed to install display driver."; exit 1; }
sudo depmod -A || { echo "Error: Failed to update module dependencies."; exit 1; }
echo 'sharp' | sudo tee -a /etc/modules
dtc -@ -I dts -O dtb -o sharp.dtbo sharp.dts || { echo "Error: Failed to compile device tree."; exit 1; }
sudo cp sharp.dtbo /boot/overlays
echo -e "framebuffer_width=400\nframebuffer_height=240\ndtoverlay=sharp" | sudo tee -a /boot/config.txt
echo -e "hdmi_force_hotplug=1\nhdmi_cvt 400 240 60 1 0 0 0\nhdmi_mode=87\nhdmi_group=2" | sudo tee -a /boot/config.txt

# Leave fbcon on /dev/fb0 because we're using snag
# sudo sed -i ' 1 s/.*/& fbcon=map:10 fbcon=font:VGA8x16/' /boot/cmdline.txt || { echo "Error: Failed to modify cmdline.txt."; exit 1; }
sudo sed -i ' 1 s/.*/& fbcon=font:VGA8x16/' /boot/cmdline.txt || { echo "Error: Failed to modify cmdline.txt."; exit 1; }

echo "Compiling and installing keyboard device driver..."
cd ~/
cd ~/beepberry_setup/keyboard
./installer.sh --BBQ20KBD_TRACKPAD_USE BBQ20KBD_TRACKPAD_AS_MOUSE --BBQX0KBD_INT BBQX0KBD_USE_INT || { echo "Error: Failed to install keyboard device driver."; exit 1; }

echo "Checking framebuffers..."
fbset -fb /dev/fb0 -i
fbset -fb /dev/fb1 -i
fbset -fb /dev/fb0 -g 400 240 400 240 16
# to take effect every boot:
# fbset >>/etc/local.fb.modes
# echo -e "fbset -db /etc/local.fb.modes --all "name of mode"" | sudo tee -a /etc/rc.local

echo "Setting terminal to display black text on white background..."
setterm -foreground black -background white
# this changes the background white to true white (instead of gray):
echo -e "\e]P7ffffff"
setterm -store

echo "snag..."
sudo apt-get -y install cmake 
sudo apt-get -y install libbsd-dev
cd ~/beepberry_setup/snag/
mkdir build
cd build
cmake ..
make
sudo make install
sudo cp ../snag.init.d /etc/init.d/snag
sudo update-rc.d snag defaults

echo "Rebooting..."
sudo reboot || { echo "Error: Failed to reboot."; exit 1; }
