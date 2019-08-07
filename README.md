# virt_cacard
Virtual CAC using libcacard, virtualsmartcard's vpcd and softhsm2 to provide PCSC accessible virtual smart card. 
---
## How it works


![schema](https://github.com/PL4typus/notes/blob/master/virt_cacard.jpg)


The [virtual pcscd](https://github.com/frankmorgner/vsmartcard/tree/master/virtualsmartcard) provides a socket based interface on one side, and the classic PCSC API on the other.

To use virt_cacard, you need a program (such as [OpenSC](https://github.com/OpenSC/OpenSC)'s tools that uses the PCSC API to communicate with smart cards. The vpcd relays those communications through its socket. This should work with any application using the PC/SC API, but virt_cacard was designed to help with the CI/CD of OpenSC.

The virtual smart card, emulated with [libcacard](https://gitlab.freedesktop.org/spice/libcacard/), connects to the socket and can then get (and reply to) APDUs from the application. From the point of view of the application, the process is transparent. 

OpenSC tests:                    [![virt_cacard](https://gitlab.com/PL4typus/OpenSC/badges/virt_cacard/pipeline.svg)](https://gitlab.com/PL4typus/OpenSC/pipelines) 

---
## How to build virt_cacard

    ./autogen.sh
    ./configure
    make
    
 
