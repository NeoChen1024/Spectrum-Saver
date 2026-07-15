# Build the bundled google/crc32c sources without depending on its CMake build.

CRC32C_ROOT := contrib/crc32c
CRC32C_CONFIG_ROOT := contrib/crc32c-config/include
CRC32C_TARGET_MACHINE := $(shell $(CXX) $(CXXFLAGS) -dumpmachine 2>/dev/null)
ifeq ($(strip $(CRC32C_TARGET_MACHINE)),)
$(error $(CXX) does not report its target machine with -dumpmachine)
endif

CRC32C_BUILD_DIR := .build/crc32c/$(CRC32C_TARGET_MACHINE)
CRC32C_LIB := $(CRC32C_BUILD_DIR)/libcrc32c.a
CRC32C_CONFIG := $(CRC32C_CONFIG_ROOT)/crc32c/crc32c_config.h

CRC32C_OBJS := \
	$(CRC32C_BUILD_DIR)/crc32c.o \
	$(CRC32C_BUILD_DIR)/crc32c_portable.o
CRC32C_PRIVATE_CPPFLAGS := -I$(CRC32C_CONFIG_ROOT)

CPPFLAGS += -I$(CRC32C_ROOT)/include

ifneq ($(filter x86_64% amd64%,$(CRC32C_TARGET_MACHINE)),)
CRC32C_OBJS += $(CRC32C_BUILD_DIR)/crc32c_sse42.o
$(CRC32C_BUILD_DIR)/crc32c_sse42.o: CRC32C_ARCH_FLAGS := -msse4.2
endif

ifneq ($(filter aarch64% arm64%,$(CRC32C_TARGET_MACHINE)),)
CRC32C_OBJS += $(CRC32C_BUILD_DIR)/crc32c_arm64.o
$(CRC32C_BUILD_DIR)/crc32c_arm64.o: CRC32C_ARCH_FLAGS := -march=armv8-a+crc+crypto
endif

CRC32C_DEPS := $(CRC32C_OBJS:.o=.d)

$(CRC32C_BUILD_DIR):
	mkdir -p $@

$(CRC32C_BUILD_DIR)/%.o: $(CRC32C_ROOT)/src/%.cc $(CRC32C_CONFIG) | $(CRC32C_BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CRC32C_PRIVATE_CPPFLAGS) $(CXXFLAGS) $(CRC32C_ARCH_FLAGS) -c -o $@ $<

$(CRC32C_LIB): $(CRC32C_OBJS)
	$(AR) rcs $@ $^
