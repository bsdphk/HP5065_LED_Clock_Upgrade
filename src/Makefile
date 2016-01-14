#
# gcc Makefile for LPC810
# Kamal Mostafa <kamal@whence.com>
#
# License: Public Domain
#

CROSS = arm-none-eabi-
CROSS = /usr/local/gcc-arm-embedded-4_9-2015q3-20150921/bin/arm-none-eabi-
CPU = -mthumb -mcpu=cortex-m0plus -MD
FPU = # -mfloat-abi=soft

FILENAME = blinky
LINKERSCRIPT = blinky.ld

CFLAGS+= -Os --specs=nano.specs -ffunction-sections -fdata-sections -fno-builtin

CFLAGS += -Wall

CC = ${CROSS}gcc
LD = ${CROSS}ld
OBJCOPY = ${CROSS}objcopy
SIZE = ${CROSS}size

# use these on the $(LD) line, as needed:
LIBM = ${shell ${CC} ${CFLAGS} --print-file-name=libm.a}
LIBC = ${shell ${CC} ${CFLAGS} --print-file-name=libc.a}
LIBGCC = ${shell ${CC} ${CFLAGS} --print-libgcc-file-name}

CFLAGS += $(CPU) $(FPU) 
LDFLAGS += --gc-sections


INC += -I../cmsis
CFLAGS += $(INC) -D__USE_CMSIS # -DCLOCK_SETUP=1

BLINKY_OBJS = main.o \
              gpio.o \
              uart.o \
              mrt.o \
              printf.o \
              printf-retarget.o \
              system_LPC8xx.o \
              gcc_startup_lpc8xx.o

all: $(FILENAME).bin

blinky.axf: $(FILENAME).ld $(BLINKY_OBJS)
	@$(LD) -o $@ $(LDFLAGS) -T $(LINKERSCRIPT) $(BLINKY_OBJS) $(LIBGCC)
	-@echo ""
	@$(SIZE) $(FILENAME).axf

clean:
	@rm -f *.o *.d
	@rm -f $(FILENAME).axf $(FILENAME).hex $(FILENAME).bin

%.bin:%.axf
	-@echo ""
	-@echo "Generating $(FILENAME).hex (FlashMagic, etc.)"
	@$(OBJCOPY) --strip-unneeded -O ihex $(FILENAME).axf $(FILENAME).hex
	-@echo "Generating $(FILENAME).bin"
	@$(OBJCOPY) --strip-unneeded -O binary $(FILENAME).axf $(FILENAME).bin

download: all
	 /usr/ports/devel/lpc21isp/work/lpc21isp-1.97/lpc21isp_197/lpc21isp \
		-NXPARM \
		-bin blinky.bin /dev/cuaU0 115200 12000

nm:
	${CROSS}nm -an $(FILENAME).axf
