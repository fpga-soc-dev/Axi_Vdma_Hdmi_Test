do_install:append () {
    install -d ${D}/etc/profile.d
    echo 'export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"' \
      > ${D}/etc/profile.d/00-path.sh
}
