# virt_cacard
Virtual CAC using libcacard, virtualsmartcard's vpcd and softhsm2 to provide PCSC accessible virtual smart card. 
---
## How it works

The [virtual pcscd](https://github.com/frankmorgner/vsmartcard/tree/master/virtualsmartcard) provides a socket based interface on one side, and the classic PCSC API on the other.

[OpenSC](https://github.com/OpenSC/OpenSC) natively uses the PCSC API to communicate with smart cards. The vpcd relays those communications through its socket. This should work with any application using the PCSC API, but virt_cacard was designed to help with the CI/CD of OpenSC.

The virtual smart card, emulated with [libcacard](https://gitlab.freedesktop.org/spice/libcacard/), connects to the socket and can then get (and reply to) APDUs from the application. From the point of view of the application, the process is transparent. 


[![Build Status](https://travis-ci.org/PL4typus/virt_cacard.svg?branch=current)](https://travis-ci.org/PL4typus/virt_cacard)

---
## How to build

    ./autogen.sh
    ./configure
    make
