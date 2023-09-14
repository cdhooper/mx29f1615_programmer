# mx29f1615_programmer

The MX29F1615 programmer is an open source hardware and software solution
which allows programming readily available 2MB Flash parts which are
pin compatible with common 27C160 EPROM parts. These parts are mostly
backward-compatible with 27C400 parts that are directly compatible with
Kickstart ROM chips common to all Amiga models since the Amiga 500.
The MX29F1615 and 27C160 parts implement up to 2MB of addressible space
by adding two extra pins which are not present on the 27C400.

The fw directory contains STM32 firmware for all board revisions.

The sw directory contains software which runs on your Linux or MacOS
host that can communicate with the STM32 firmware to read / erase / program
the MX29F1615 EEPROM.


Building mxprog from Linux:
<PRE>
    git clone https://github.com/cdhooper/mx29f1615_programmer
    cd mx29f1615_programmer/sw
    make
</PFE>

Installation
<PRE>
    sudo cp mxprog /usr/local/bin
    sudo chown root.root /usr/local/bin/mxprog
    sudo chmod 4755 /usr/local/bin/mxprog
</PRE>
