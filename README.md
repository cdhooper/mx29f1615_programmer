# mx29f1615_programmer

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
