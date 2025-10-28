LICENSE = "CLOSED"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = "\
    file://CMakeLists.txt \
    file://main.cpp \
    file://Gpio/Gpio.cpp \
    file://Gpio/Gpio.hpp \
    file://TpgDriver/TpgDriver.cpp \
    file://TpgDriver/TpgDriver.hpp \
    file://Utilities/FileHandle.cpp \
    file://Utilities/FileHandle.hpp \
    "

S = "${WORKDIR}"

do_rm_work[noexec] = "1"

DEPENDS += " fmt libgpiod spdlog opencv gstreamer1.0  gstreamer1.0-plugins-base"

inherit cmake pkgconfig

do_install() {
    install -d ${D}/home/root
    install -m 0755 ${B}/tpg-overlay ${D}/home/root/
}

FILES:${PN} += "/home/root/tpg-overlay"
