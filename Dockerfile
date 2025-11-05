# NetBox - 企业级跨平台网络框架 Docker镜像
# 使用多阶段构建优化镜像大小

# 第一阶段：构建阶段
FROM ubuntu:22.04 AS builder

# 设置环境变量
ENV DEBIAN_FRONTEND=noninteractive
ENV CMAKE_VERSION=3.25.1
ENV GCC_VERSION=11

# 使用官方源（通常更稳定）
# 如果下载慢，可以尝试使用清华源：
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y tzdata \
    && rm -rf /var/lib/apt/lists/*
# 配置上海时区
ENV TZ=Asia/Shanghai
# 手动创建 localtime 软链接（alpine 需显式配置）
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime \
    && echo $TZ > /etc/timezone

 RUN sed -i 's/archive.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list && \
     sed -i 's/security.ubuntu.com/mirrors.tuna.tsinghua.edu.cn/g' /etc/apt/sources.list

# 安装构建依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    pkg-config \
    libssl-dev \
    libboost-all-dev \
    libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /app

# 复制源代码
COPY . .

# 创建构建目录
RUN mkdir -p build && cd build

# 配置和构建项目
RUN cd build && \
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O3 -DNDEBUG" \
    && make -j$(nproc)

# 第二阶段：运行阶段
FROM ubuntu:22.04 AS runtime

# 更换为国内镜像源以提高下载速度
RUN sed -i 's/archive.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list && \
    sed -i 's/security.ubuntu.com/mirrors.aliyun.com/g' /etc/apt/sources.list

# 安装运行时依赖
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    libgcc-s1 \
    libspdlog1 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 创建非root用户
RUN useradd -r -s /bin/false netbox

# 创建应用目录
WORKDIR /app

# 从构建阶段复制可执行文件和配置文件
COPY --from=builder /app/build/bin/NetBox /app/NetBox
COPY --from=builder /app/config /app/config
COPY --from=builder /app/plugins /app/plugins

# 创建日志目录
RUN mkdir -p /app/logs && chown -R netbox:netbox /app

# 切换到非root用户
USER netbox

# 暴露端口（根据配置文件中的端口）
EXPOSE 6379 8888 8001

# 健康检查
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD ps aux | grep NetBox || exit 1
# 启动命令
CMD ["./NetBox", "config/config-docker.yaml"]
