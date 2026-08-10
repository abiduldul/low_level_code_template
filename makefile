# Define phony targets so Make doesn't confuse them with actual files
.PHONY: all build-mertani-2.5 build-mertani-2.5_HAL build-linux clean

app ?= app_testing_awl_2_5

# Default rule when you just type 'make'
all: build-linux build-mertani-2.5 build-mertani-2.5_HAL

run: flash run-linux

run-linux:
	$(MAKE) -C targets/linux/mertani-2.5_simulator run
	build/linux_sim/watchdog build/linux_sim/linux_sim

flash:
	openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/mertani-2.5/firmware.elf verify reset exit"

flash_HAL:
	openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/mertani-2.5_HAL/firmware.elf verify reset exit"

# Rule to configure and build for the Mertani 2.5 hardware
build-mertani-2.5:
	@echo "--- Configuring and Building for Mertani 2.5 Hardware ---"
	cmake --preset mertani-2.5 -DAPP=$(app)
	cmake --build --preset build-mertani-2.5 -DAPP=$(app)
	arm-none-eabi-size build/mertani-2.5/firmware.elf

build-mertani-2.5_HAL:
	@echo "--- Configuring and Building for Mertani 2.5 HAL Hardware ---"
	cmake --preset mertani-2.5_HAL -DAPP=$(app)
	cmake --build --preset build-mertani-2.5_HAL 
	arm-none-eabi-size build/mertani-2.5_HAL/firmware.elf

# Rule to configure and build for the Linux simulation
build-linux:
	@echo "--- Configuring and Building for Linux Simulation ---"
	cmake --preset linux -DAPP=$(app)
	cmake --build --preset build-linux -DAPP=$(app)
	$(MAKE) -C targets/linux/src/watchdog build
	$(MAKE) -C targets/linux/mertani-2.5_simulator build-mertani-2.5

clean-linux:
	rm -rf targets/linux/mertani-2.5_simulator/build/
# Rule to completely wipe the generated build files
clean: clean-linux
	@echo "--- Cleaning build directory ---"
	rm -rf build/