cmd_arch/arm/boot/compressed/vmlinux.lds := arm-linux-gnueabihf-gcc -E -Wp,-MD,arch/arm/boot/compressed/.vmlinux.lds.d  -nostdinc -isystem /usr/lib/gcc-cross/arm-linux-gnueabihf/5/include -I./arch/arm/include -I./arch/arm/include/generated  -I./include -I./arch/arm/include/uapi -I./arch/arm/include/generated/uapi -I./include/uapi -I./include/generated/uapi -include ./include/linux/kconfig.h -D__KERNEL__ -mlittle-endian     -DTEXT_START="0" -DBSS_START="ALIGN(8)" -P -C -Uarm -D__ASSEMBLY__ -DLINKER_SCRIPT -o arch/arm/boot/compressed/vmlinux.lds arch/arm/boot/compressed/vmlinux.lds.S

source_arch/arm/boot/compressed/vmlinux.lds := arch/arm/boot/compressed/vmlinux.lds.S

deps_arch/arm/boot/compressed/vmlinux.lds := \
    $(wildcard include/config/cpu/endian/be8.h) \

arch/arm/boot/compressed/vmlinux.lds: $(deps_arch/arm/boot/compressed/vmlinux.lds)

$(deps_arch/arm/boot/compressed/vmlinux.lds):
