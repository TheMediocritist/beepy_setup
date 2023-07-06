### setterm from ssh:
```
setterm -term linux -back white -fore black --inversescreen=off -clear > /dev/tty0 -store
```
### Pre-compiled love2d for Raspberry Pi Zero on Buster:
```
wget https://files.retropie.org.uk/binaries/buster/rpi1/ports/love.tar.gz
```
### And minivmac
```
wget https://files.retropie.org.uk/binaries/bullseye/rpi1/kms/emulators/minivmac.tar.gz.asc 
```
### Install drm modetest
```
sudo apt-get install libdrm-tests
modetest -c
```
### Also useful
```
vcgencmd dispmanx_list
```
### Installing mgba - instructions on Github don't work, but this does
```
mkdir build
cd build
```
For Pi Zero 2:
```
cmake -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_CXX_FLAGS="-march=armv8-a -mtune=cortex-a72" ..
```
For Pi Zero
```
cmake -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_CXX_FLAGS="-march=armv6-a -mtune=cortex-a72" ..
```
Then
```
make
sudo make install
```
### Install SDL
Using Simple2d's SDL installer
```
simple2d install --sdl
```
Using RetroPie
```
instructions
```
