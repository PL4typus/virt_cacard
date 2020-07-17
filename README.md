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


# Usage

The fastest way to start is to install virtualsmartcard's module
vpcd for pcscd (see above), configure softhsm with default certificates
and start `virt_cacard`:

    $ . setup-softhsm2.sh
    $ ./virt_cacard

After that, you should be able to access virtual smart card through OpenSC:

    $ pkcs11-tool -L
    Available slots:
    Slot 0 (0x0): Virtual PCD 00 00
      token label        : CAC II
      token manufacturer : Common Access Card
      token model        : PKCS#15 emulated
      token flags        : login required, rng, token initialized, PIN initialized
      hardware version   : 0.0
      firmware version   : 0.0
      serial num         : 000058bd002c19b5
      pin min/max        : 4/8
    Slot 1 (0x4): Virtual PCD 00 01
      (empty)
    Slot 2 (0x8): Virtual PCD 00 02
      (empty)
    Slot 3 (0xc): Virtual PCD 00 03
      (empty)

If you use Fedora or RHEL, make sure to configure p11-kit to not load OpenSC
for `virt_cacard`:

   # echo "disable-in: virt_cacard" >> /usr/share/p11-kit/modules/opensc.module

otherwise the above command will hang (recursive access to pcscd).

The virtual smart card is automatically started in inserted state. To simulate
smart card removal and insertion events, you can either kill the `virt_cacard`
process or send SIGUSR1 to remove the card and SIGUSR2 to reinsert the card again.

This can be done by running `virt_cacard` with `-r` switch to remove card and
`-i` to insert card back again. This will automatically send signals to all
`virt_cacard` processes in the system. If you want to limit which process should
be used (if there are multiple virtual smart cards or `virt_cacard` runs under
valgrind), you can use `-p` switch to specify PID.

## Debugging

to get debug logs from running `virt_cacard`, set `G_MESSAGES_DEBUG=virt_cacard`
environment variable for the process, for example:

    G_MESSAGES_DEBUG=virt_cacard ./virt_cacard

To get debug logs from `libcacard`, use the same variable with `libcacard` value.
