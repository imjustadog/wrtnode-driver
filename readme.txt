it is a simplified device driver template.

the directory should actually be located at:
/home/adoge/openwrt-fw/package/kernel/i2c-dog

compile command:
adoge@adoge-PC:~/openwrt-fw$ sudo make package/kernel/i2c-dog/compile V=s
(remember to set the menu in make menuconfig to [M])

the genrated .ko is located at:
~/openwrt-fw/build_dir/target-mipsel_24kec+dsp_uClibc-0.9.33.2/linux-ramips_mt7620/i2c-dog


