# Software CitizenSensor
> A python code, which uses tkinter to create a GUI that leads step-by-step through the different processes of a nitrate measurement.

## Hardware

Runs on a raspbian system with Python 3

# Setting up Raspberry Pi

In order to get the programm running in autostart several preparations have to be executed.

## Autostart of the python application
In the file `/etc/xdg/lxsession/LXDE-pi/autostart`, add:
```
#@lxpanel --profile LXDE-pi
#@pcmanfm --desktop --profile LXDE-pi
@/usr/bin/python3 /home/pi/work/main.py
#@xscreensaver -no-splash
#point-rpi   

cd ~/.config
mkdir lxsession
cd lxsession
mkdir lxde-pi
cd ~
nano ./.config/lxsession/lxde-pi/autostart
```
At the end of the file `~/.bashrc`, add:
```
export DISPLAY=:0.0
```
In the file `/boot/cmdline.txt`, change
```
console=tty1  
```
to
```
console=tty1  
```
and add at the end of the line:
```
logo.nologo quiet splash plymouth.ignore-serial-consoles vt.global_cursor_default=0
```
In the file `/etc/rc.local` insert:
```
ifconfig wlan0|grep "inet "|awk  '{print $2}' > /home/pi/work/lastip
```
before
```
exit 0
```

### Troubleshooting

Dependent on your raspbian version, the changes in `lxsession/lxde-pi/autostart` has also be done to `/home/pi/.config/lxsession/lxde-pi/autostart`

## Make the mousepointer invisible
In the file `/etc/X11/xinit/xserverrc` search for:
```
exec /usr/bin/X
```
and add to the end of the line:
```
-nocursor
```
In the file `/etc/lightdm/lightdm.conf` search for a section `[Seat:*]` with `xserver-command=X`:   
remove the `#` and add to the end of the command:
```
-nocursor
```

## Deacitvate screen blanking
In the file `/etc/xdg/lxsessions/LXDE-pi/autostart` add at the end:
```
@xset s noblank
@xset s off
@xset -dpms
```

## Activate autologin and start to X-desktop
Type the shell-command:
```
sudo raspi-config
```
and choose `3 Boot Options → B1 Desktop / CLI → B4 Desktop Autologin Desktop GUI, automatically logged in as 'pi' user`   
also choose `wait for network at boot`

## Get the serial communication between the Raspberry and the HAT working
Add to the file `/boot/config.txt`:
```
dtoverlay=pi3-disable-bt
```
Type the shell-command:
```
sudo raspi-config
```
and enable the serial port.

## Setting up MySQL

Follow the instructions here: https://tutorials-raspberrypi.de/webserver-installation-apache2/   
Use the credentials in the mysql-credentials file.

### Troubleshooting
If the instructions do not work for your version of raspbian, have a look in the comments to this tutorial on the website.

## Installing the display driver

Use the instructions here: https://www.joy-it.net/de/products/RB-TFT3.5

## Install the required python packages:
```
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install python3-mysqldb
sudo apt-get install python3-mysql.connector
sudo apt install python3-matplotlib
sudo apt install python3-netifaces
sudo apt install python3-usb
python3 -m pip install https://github.com/martinohanlon/KY040/archive/master.zip
python3 -m pip install https://github.com/tatobari/hx711py/archive/master.zip
sudo apt-get install python3-numpy
sudo apt install python3-pandas
```

## Clone this repository into your raspberry Pi
```
mkdir work
cd work
git clone https://github.com/CitizenSensor/CitizenSensor.git
```


