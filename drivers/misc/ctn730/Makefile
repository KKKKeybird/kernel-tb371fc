# TASK-079 stock-parity rebuild: ctn730.c is self-contained (no crc32.c,
# no CTN730_i2c.c variant) and matches the factory single-object layout.
ifeq  ($(CONFIG_NXP_VERSION),y)
obj-$(CONFIG_NFC_NXP_CTN730) += CTN730_i2c.o
else
obj-$(CONFIG_NFC_NXP_CTN730) += ctn730.o
endif
