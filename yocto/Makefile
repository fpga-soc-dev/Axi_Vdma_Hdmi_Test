BUILD_DIR = $(abspath build)
SHELL = bash
.SHELLFLAGS += -e

.PHONY: build_yocto_project
build_yocto_project:
	@echo "Building Yocto Project..."
	kas build build.yaml

.PHONY: clean
clean:
	@echo "Cleaning Yocto Project build directory..."
	rm -rf $(BUILD_DIR)
