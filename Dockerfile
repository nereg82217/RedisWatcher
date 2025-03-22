# 第一階段：編譯環境
FROM ubuntu:25.04 AS builder

# 设置非交互模式，防止安装过程中卡住
ENV DEBIAN_FRONTEND=noninteractive
#ENV DOCKER=1

# 安装编译依赖
RUN apt-get update && apt-get install -y \
    cmake gcc g++ gdb ninja-build \
    libglib2.0-dev pkg-config libevent-dev libhiredis-dev libcurl4-openssl-dev libjansson-dev \
    && \
    apt-get clean

# ========== 拷貝源碼 ==========
COPY . /RedisWatcher

# ========== 設置工作目錄 ==========
WORKDIR /RedisWatcher

# ========== 編譯 ==========
RUN /usr/bin/cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=/bin/ninja -DCMAKE_C_COMPILER=/bin/gcc -DCMAKE_CXX_COMPILER=/bin/g++ -G Ninja -S /RedisWatcher -B /RedisWatcher/build
RUN /usr/bin/cmake --build /RedisWatcher/build --target RedisWatcher -j $(nproc)

# ========== 運行安裝 ==========
RUN /usr/bin/cmake --install /RedisWatcher/build


# 第二階段：最小運行環境（只保留可執行檔）
FROM ubuntu:25.04

COPY --from=builder /opt/redis-watcher/bin/RedisWatcher /opt/redis-watcher/bin/RedisWatcher

# 添加所需的運行時依賴（glib、curl 等最小子集）
RUN apt-get update && apt-get install -y --no-install-recommends \
    libglib2.0-dev pkg-config libevent-dev libhiredis-dev libcurl4-openssl-dev libjansson-dev \
    ca-certificates \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
