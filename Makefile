CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -MMD -MP
LDFLAGS  :=

# ======== 编译器 (leash) ========
SRCDIR    := src
OBJDIR    := obj
COMMON_SRCS := frontend/lexer.cpp frontend/parser.cpp checker/typecheck.cpp \
                codegen/compiler.cpp codegen/llvmgen.cpp vm/vm.cpp host/host.cpp host/native.cpp \
                common/json.cpp
COMMON_OBJS := $(addprefix $(OBJDIR)/, $(patsubst %.cpp,%.o,$(COMMON_SRCS)))
DIRS := $(sort $(dir $(COMMON_OBJS)))

TARGET := leash

.PHONY: all
all: $(TARGET) $(STUDIO_BIN)

$(DIRS):
	mkdir -p $@

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(DIRS)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c $< -o $@

$(OBJDIR)/main.o: $(SRCDIR)/main.cpp | $(DIRS)
	$(CXX) $(CXXFLAGS) -I$(SRCDIR) -c $< -o $@

$(TARGET): $(OBJDIR)/main.o $(COMMON_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

# ======== IDE / Studio (leash-studio) ========
STUDIO_SRC_DIR := studio/src
STUDIO_BLD_DIR := obj/studio
STUDIO_BIN     := leash-studio

STUDIO_SRCS := $(shell find $(STUDIO_SRC_DIR) -name '*.cpp')
STUDIO_OBJS := $(patsubst $(STUDIO_SRC_DIR)/%.cpp,$(STUDIO_BLD_DIR)/%.o,$(STUDIO_SRCS))
STUDIO_DIRS := $(sort $(dir $(STUDIO_OBJS)))

$(STUDIO_DIRS):
	mkdir -p $@

$(STUDIO_BLD_DIR)/%.o: $(STUDIO_SRC_DIR)/%.cpp | $(STUDIO_DIRS)
	$(CXX) $(CXXFLAGS) -I$(STUDIO_SRC_DIR) -c $< -o $@

$(STUDIO_BIN): $(STUDIO_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

# ======== 全局目标 ========
.PHONY: all clean run run-ide

all: $(TARGET) $(STUDIO_BIN)

run: $(TARGET)
	./$(TARGET) examples/hello.ae

run-ide: $(STUDIO_BIN)
	./$(STUDIO_BIN)

clean:
	rm -rf $(OBJDIR) $(STUDIO_BLD_DIR) $(TARGET) $(STUDIO_BIN)

-include $(wildcard $(OBJDIR)/*.d) $(wildcard $(STUDIO_BLD_DIR)/**/*.d)
