FROM nvidia/cudagl:11.4.2-runtime-ubuntu18.04

ARG DEBIAN_FRONTEND=noninteractive

# Use custom apt sources if provided in the build context
COPY setup/sources.list /etc/apt/sources.list

RUN apt-get update

RUN dpkg --add-architecture amd64
RUN dpkg --add-architecture arm64

RUN apt-get update

ENV LIBGL_ALWAYS_INDIRECT=0
ENV __GLX_VENDOR_LIBRARY_NAME=nvidia

# Install build and runtime deps (keep to --no-install-recommends to avoid pulling unwanted Mesa/NVIDIA driver packages)
RUN apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    file \
    dpkg-dev \
    qemu \
    binfmt-support \
    qemu-user-static \
    pkg-config \
    ninja-build \
    doxygen \
    clang \
    python3 \
    gcc \
    g++ \
    git \
    git-lfs \
    nasm \
    cmake \
    libsoundio-dev:amd64 \
    libjpeg-dev:amd64 \
    libvulkan-dev:amd64 \
    libx11-dev:amd64 \
    libxcursor-dev:amd64 \
    libxinerama-dev:amd64 \
    libxrandr-dev:amd64 \
    libusb-1.0-0-dev:amd64 \
    libssl-dev:amd64 \
    libudev-dev:amd64 \
    uuid-dev:amd64 \
    libopencv-dev:amd64 \
    ca-certificates \
    software-properties-common \
    wget \
    apt-transport-https \
    gnupg2 \
    build-essential \
    curl \
    unzip \
    binutils \
    libglew-dev \
    libzmq3-dev \
    libomp5 \
    libusb-1.0-0-dev \
    pkg-config

# Add NVIDIA CUDA repo key (keeps CUDA tooling available if needed)
RUN wget -qO - https://developer.download.nvidia.com/compute/cuda/repos/ubuntu1804/x86_64/7fa2af80.pub | apt-key add - && apt-get update

# Minimal GLVND and EGL runtime libraries (do not install full Mesa runtime stacks)
RUN apt-get install -y --no-install-recommends \
    libglvnd0 \
    libglvnd-dev \
    libegl1 \
    libgles2

# DO NOT install an in-container NVIDIA driver package here.
# The container should use the host's NVIDIA user-space libraries via bind-mounts at runtime
# to ensure kernel module / user-space driver version compatibility.

# Add Microsoft repo for Azure Kinect packages
RUN wget -q https://packages.microsoft.com/config/ubuntu/18.04/packages-microsoft-prod.deb && dpkg -i packages-microsoft-prod.deb && rm -f packages-microsoft-prod.deb
RUN update-ca-certificates

RUN wget -qO- https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor -o /usr/share/keyrings/microsoft.gpg
RUN echo "deb [signed-by=/usr/share/keyrings/microsoft.gpg] https://packages.microsoft.com/ubuntu/18.04/prod bionic main" \
    | tee /etc/apt/sources.list.d/microsoft.list
RUN apt-get update

# Accept EULA for k4a packages via debconf
RUN echo libk4a1.4       libk4a1.4/accept-eula           boolean true | debconf-set-selections && \
    echo libk4a1.4       libk4a1.4/accepted-eula-hash    string  0f5d5c5de396e4fee4c0753a21fee0c1ed726cf0316204edda484f08cb266d76 | debconf-set-selections && \
    echo libk4abt1.1     libk4abt1.1/accept-eula         boolean true | debconf-set-selections && \
    echo libk4abt1.1     libk4abt1.1/accepted-eula-hash  string  03a13b63730639eeb6626d24fd45cf25131ee8e8e0df3f1b63f552269b176e38 | debconf-set-selections

# Install the Azure Kinect SDK packages
RUN apt-get install -y --no-install-recommends \
    libk4a1.4=1.4.1 libk4a1.4-dev=1.4.1 \
    libk4abt1.1=1.1.2 libk4abt1.1-dev=1.1.2 \
    k4a-tools=1.4.1

RUN apt-get autoremove -y && apt-get clean && rm -rf /var/lib/apt/lists/*

# Install cppzmq (C++ bindings for ZeroMQ)
RUN set -eux; \
    cd /tmp; \
    git clone --depth 1 --branch v4.7.1 https://github.com/zeromq/cppzmq.git; \
    cd cppzmq; mkdir build; cd build; cmake .. -DCPPZMQ_BUILD_TESTS=OFF; make -j$(nproc) install; \
    cd /tmp; rm -rf cppzmq

# Create pkg-config files for k4a and k4abt if missing
RUN mkdir -p /usr/lib/pkgconfig
RUN printf '%s\n' "prefix=/usr" "exec_prefix=\${prefix}" "libdir=\${exec_prefix}/lib" "includedir=\${prefix}/include" "" \
  "Name: k4a" "Description: Azure Kinect Sensor SDK" "Version: 1.4.1" "Libs: -L\${libdir} -lk4a" "Cflags: -I\${includedir}/k4a" \
  > /usr/lib/pkgconfig/libk4a.pc
RUN printf '%s\n' "prefix=/usr" "exec_prefix=\${prefix}" "libdir=\${exec_prefix}/lib" "includedir=\${prefix}/include" "" \
  "Name: k4abt" "Description: Azure Kinect Body Tracking SDK" "Version: 1.1.2" "Libs: -L\${libdir} -lk4abt" "Cflags: -I\${includedir}/k4abt" \
  > /usr/lib/pkgconfig/libk4abt.pc

# Install single-header nlohmann/json
RUN mkdir -p /usr/local/include/nlohmann \
    && wget -qO /usr/local/include/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp

COPY . /opt/k4a_server
WORKDIR /opt/k4a_server

# Build the project
RUN mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && cmake --build . -- -j$(nproc)

RUN set -eux; \
    echo "[cleanup] scanning for conflicting NVIDIA GL libraries"; \
    for pattern in \
      /usr/lib/x86_64-linux-gnu/libGLX_nvidia.so.* \
      /usr/lib/x86_64-linux-gnu/libGL.so.* \
      /usr/lib/x86_64-linux-gnu/libEGL.so.* \
    ; do \
      found=0; \
      for f in $pattern; do \
        if [ -e "$f" ]; then \
          found=1; \
          echo "[cleanup] removing $f"; \
          rm -vf "$f"; \
        fi; \
      done; \
      if [ "$found" -eq 0 ]; then \
        echo "[cleanup] no matches for pattern: $pattern"; \
      fi; \
    done

RUN apt-get autoremove -y && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
  echo "[cleanup] scanning for conflicting NVIDIA GL libraries"; \
  for f in /usr/lib/x86_64-linux-gnu/libGLX_nvidia.so.* /usr/lib/x86_64-linux-gnu/libGL.so.* /usr/lib/x86_64-linux-gnu/libEGL.so.*; do \
    if [ -e "$f" ]; then echo "[cleanup] removing $f"; rm -vf "$f"; else echo "[cleanup] not present: $f"; fi; \
  done

ENTRYPOINT ["/opt/k4a_server/build/k4a_server"]
