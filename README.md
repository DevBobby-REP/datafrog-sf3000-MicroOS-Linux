# 🐸 datafrog-sf3000-os-devkit

**Welcome to MicroOS-Linux! This is an entirely new system for the Data Frog SF3000.**
---

## COMPONENT DETAILS

### MicroOS-Linux
**MicroOS-Linux is a brand new system for the console.** It already includes a desktop environment, a terminal, and full support for a mouse and keyboard.

> [!WARNING]
> **THE SYSTEM IS STILL IN A VERY EARLY, RAW STATE! IT HAS ALMOST NO FUNCTIONALITY, AND COMMAND `nano` DO NOT WORK IN THE TERMINAL YET.**

---

### modules-for-sf3000
**Modules for the SF3000, specifically for its Linux kernel (files with the `.ko` extension).** 
* Core modules such as `evdev.ko` work on this console **WITHOUT ANY MODIFICATIONS** to their code.
* This folder serves as a repository for these `.ko` kernel modules, intended to assist other developers working on this console.
* g_mass_storage.ko DOESN'T WORK AS THE CONSOLE IS THE HOST! Well, if someone manages to bypass this, just use this module.

---

## LICENSE & ACKNOWLEDGMENTS

* **LVGL Port / MicroOS:** Distributed under the **MIT License**.
* **Kernel Modules:** Based on upstream Linux Kernel sources.
* Built using the MIPS32r2 / MIPS74Kc toolchain.
