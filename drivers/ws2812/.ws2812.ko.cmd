cmd_drivers/ws2812/ws2812.ko := arm-linux-gnueabihf-ld -EL -r  -T ./scripts/module-common.lds --build-id  -o drivers/ws2812/ws2812.ko drivers/ws2812/ws2812.o drivers/ws2812/ws2812.mod.o ;  true
