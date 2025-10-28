FILESEXTRAPATHS:prepend := "${THISDIR}/files:${SYSCONFIG_PATH}:"

KERNEL_INCLUDE:append = " \
        ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts \
        ${STAGING_KERNEL_DIR}/arch/${ARCH}/boot/dts/* \
        ${STAGING_KERNEL_DIR}/scripts/dtc/include-prefixes \
        "

        
SYSTEM_USER_DTSI ?= "system-user.dtsi"
SRC_URI:append = " file://${SYSTEM_USER_DTSI}"
 
 
do_configure:append() {
        cp ${WORKDIR}/${SYSTEM_USER_DTSI} ${B}/device-tree
        sed -i '/#include "zc702.dtsi"/d' ${B}/device-tree/system-top.dts
        echo "/include/ \"${SYSTEM_USER_DTSI}\"" >> ${B}/device-tree/system-top.dts
}
