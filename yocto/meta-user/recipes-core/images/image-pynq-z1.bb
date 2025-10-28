SUMMARY = "Image for pynq z1 board"

IMAGE_LINGUAS = " "

LICENSE = "MIT"

inherit core-image

IMAGE_INSTALL = "packagegroup-core-boot"
IMAGE_INSTALL:append = " ${CORE_IMAGE_EXTRA_INSTALL}"
IMAGE_INSTALL:append = " kernel-modules"
IMAGE_INSTALL:append = " openssh openssh-sshd openssh-sftp-server"
IMAGE_INSTALL:append = " gstreamer1.0 gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-bad v4l-utils"
IMAGE_INSTALL:append = " libgpiod libgpiod-dev libgpiod-tools"
IMAGE_INSTALL:append = " opencv opencv-apps opencv-dbg opencv-dev opencv-samples"
IMAGE_INSTALL:append = " usbutils"
IMAGE_INSTALL:append = " digilent-hdmi tpg-overlay"
IMAGE_INSTALL:append = " libdrm libdrm-drivers libdrm-tests"

inherit extrausers
EXTRA_USERS_PARAMS:append = "usermod -d /home/root root;"
