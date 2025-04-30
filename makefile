# Makefile

# 编译器和选项
CXX      := g++
CXXFLAGS := -std=c++20 -I./HttpServer/include -I./ProxyServer/include -Wall -Wextra -pthread

# 源码目录
HS_SRCDIR := HttpServer/src
PS_SRCDIR := ProxyServer/src

# 对应的头文件路径
HS_INCDIR := HttpServer/include
PS_INCDIR := ProxyServer/include

# 目标可执行文件
HS_TARGET := http-server
PS_TARGET := proxy-server

# 自动搜集源文件
HS_SRCS := $(wildcard $(HS_SRCDIR)/*.cpp)
PS_SRCS := $(wildcard $(PS_SRCDIR)/*.cpp)

# 对应的对象文件
HS_OBJS := $(HS_SRCS:$(HS_SRCDIR)/%.cpp=build/hs_%.o)
PS_OBJS := $(PS_SRCS:$(PS_SRCDIR)/%.cpp=build/ps_%.o)

.PHONY: all http-server proxy-server clean dirs

all: $(HS_TARGET) $(PS_TARGET)

# 构建 http-server
http-server: dirs $(HS_TARGET)
$(HS_TARGET): $(HS_OBJS)
	$(CXX) $^ -o $@ $(CXXFLAGS)

# 构建 proxy-server
proxy-server: dirs $(PS_TARGET)
$(PS_TARGET): $(PS_OBJS)
	$(CXX) $^ -o $@ $(CXXFLAGS)

# 通用：生成 build 目录
dirs:
	@mkdir -p build

# 规则：HttpServer 对应 .cpp -> build/hs_*.o
build/hs_%.o: $(HS_SRCDIR)/%.cpp $(HS_INCDIR)/*.h
	$(CXX) $(CXXFLAGS) -I$(HS_INCDIR) -c $< -o $@

# 规则：ProxyServer 对应 .cpp -> build/ps_*.o
build/ps_%.o: $(PS_SRCDIR)/%.cpp $(PS_INCDIR)/*.h
	$(CXX) $(CXXFLAGS) -I$(PS_INCDIR) -c $< -o $@

clean:
	@rm -rf build $(HS_TARGET) $(PS_TARGET)
