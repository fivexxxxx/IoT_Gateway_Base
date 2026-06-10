# ============================================================================
# 交叉编译配置 (ARM Linux)
# ============================================================================
# 交叉编译器前缀 (Ubuntu 官方仓库标准命名，与arm-linux- 软链接兼容)
CROSS_COMPILE ?= arm-linux-gnueabihf-
CC      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
STRIP   := $(CROSS_COMPILE)strip

# 基础标志 
BASE_CFLAGS := -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE -D_DEFAULT_SOURCE -O2 -DNDEBUG

#  忽略未使用参数警告
override CFLAGS := $(BASE_CFLAGS) -Wno-unused-parameter

# 头文件搜索路径
INCLUDE := -I./include -I./src $(addprefix -I, $(shell find ./include -mindepth 1 -type d))

# 链接标志 
STATIC  ?= 1
LDFLAGS := -lpthread
ifeq ($(STATIC), 1)
  LDFLAGS += -static
endif

# 目标命名 
DEBUG   ?= 0
ifeq ($(DEBUG), 1)
  override CFLAGS := -Wall -Wextra -std=c11 -D_GNU_SOURCE -D_DEFAULT_SOURCE -g -O0 -DDEBUG -Wno-unused-parameter
  TARGET  := server_arm_debug
else
  override CFLAGS := -Wall -Wextra -Werror -std=c11 -D_GNU_SOURCE -D_DEFAULT_SOURCE -O2 -DNDEBUG -Wno-unused-parameter
  TARGET  := server_arm
endif

# ============================================================================
# 自动收集源文件与目标文件
# ============================================================================
SRCS    := $(shell find src -name '*.c')
OBJS    := $(SRCS:.c=.o)
DEPS    := $(OBJS:.o=.d)

# ============================================================================
# 构建规则
# ============================================================================
.PHONY: all clean strip deploy

all: $(TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)
	@echo " Cross-build success: $@ ($(if $(filter 1,$(DEBUG)),Debug,Release)) for ARM Linux"

# 编译 
%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDE) -MMD -MP -c $< -o $@

# 包含自动生成的依赖文件
-include $(DEPS)

# 体积裁剪 
strip: $(TARGET)
	$(STRIP) $<
	@echo " Stripped binary: $< (size reduced)"

# 部署提示 
deploy: $(TARGET)
	@echo " ARM binary ready. Deploy steps:"
	@echo "   1. 传输到目标板: scp $(TARGET) root@<TARGET_IP>:/usr/local/bin/"
	@echo "   2. 赋予执行权限: ssh root@<TARGET_IP> 'chmod +x /usr/local/bin/$(TARGET)'"
	@echo "   3. 远程运行:      ssh root@<TARGET_IP> '/usr/local/bin/$(TARGET)'"

# 清理构建产物
clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	find . -type d -empty -delete 2>/dev/null || true
	@echo " Cleaned."