#!/usr/bin/env bash
# run.sh - manage the k4a-native-server:modular Docker image and container
# Usage: ./run.sh build|up [--debug] [--shell]|down|status|logs
set -euo pipefail

IMAGE_NAME="k4a-native-server:modular"
CONTAINER_NAME="k4a-native"
HOST_USB_PATH="/dev/bus/usb"
DOCKER_CONTEXT="."
BUILD_IF_MISSING=${BUILD_IF_MISSING:-1}
EXTRA_DOCKER_ARGS=${EXTRA_DOCKER_ARGS:-""}
# OPTIONAL: set HOST_NVIDIA_LIB_DIR to bind-mount host nvidia libs into container
HOST_NVIDIA_LIB_DIR=${HOST_NVIDIA_LIB_DIR:-"/usr/lib/x86_64-linux-gnu/nvidia"}
GID=$(getent group plugdev | cut -d: -f3 || true)

function help_msg() {
  cat <<EOF
Usage: $0 <command> [options]
Commands:
  build                Build the Docker image (${IMAGE_NAME})
  up [--debug] [--shell]
                       Start the container (builds image first if missing).
                       --debug  sets K4A_LOG_LEVEL=TRACE inside the container
                       --shell  overrides entrypoint to /bin/bash (interactive)
  down                 Stop and remove the container
  status               Show container status
  logs                 Stream container logs (ctrl-C to exit)
ENV:
  HOST_NVIDIA_LIB_DIR  Path on host to NVIDIA user-mode libs to bind-mount (optional)
  EXTRA_DOCKER_ARGS    Extra docker args (quoted)
EOF
}

function image_exists() {
  docker image inspect "${IMAGE_NAME}" >/dev/null 2>&1
}

function build_image() {
  echo "[run.sh] Building Docker image ${IMAGE_NAME} ..."
  docker build --progress=plain -t "${IMAGE_NAME}" "${DOCKER_CONTEXT}"
  echo "[run.sh] Build finished."
}

function stop_container_if_exists() {
  if docker ps -a --format '{{.Names}}' | grep -x "${CONTAINER_NAME}" >/dev/null 2>&1; then
    echo "[run.sh] Stopping and removing existing container ${CONTAINER_NAME} ..."
    docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
  fi
}

# Build base docker args into a passed array variable name
function base_docker_args_array() {
  local -n _out=$1
  _out=()

  _out+=(--rm)
  _out+=(--name "${CONTAINER_NAME}")
  _out+=(--net=host)
  # keep privileged for USB passthrough (Kinect). If you prefer stricter device exposure,
  # replace with explicit --device entries for the Kinect nodes.
  _out+=(--privileged)

  # USB passthrough
  _out+=(-v "${HOST_USB_PATH}:${HOST_USB_PATH}")
#  _out+=(--device /dev/kinect_depth:/dev/kinect_depth)
#  _out+=(--device /dev/kinect_color:/dev/kinect_color)
#  _out+=(--device /dev/kinect_hub_ss:/dev/kinect_hub_ss)
#  _out+=(--device /dev/kinect_hub:/dev/kinect_hub)
#  _out+=(--device /dev/kinect_mic:/dev/kinect_mic)

  # Nvidia device nodes commonly required
  _out+=(-v /dev/nvidiactl:/dev/nvidiactl -v /dev/nvidia-uvm:/dev/nvidia-uvm -v /dev/nvidia0:/dev/nvidia0)

  # X11 / DISPLAY
  local host_display="${DISPLAY:-:0}"
  _out+=(-e "DISPLAY=${host_display}")
  _out+=(-v /tmp/.X11-unix:/tmp/.X11-unix:ro)
  _out+=(-v "$HOME/.Xauthority:/root/.Xauthority:rw")
  _out+=(-e "XAUTHORITY=/root/.Xauthority")

  # Bind host libGL and EGL so container uses host NVIDIA user-space libs
  _out+=(-v /usr/lib/x86_64-linux-gnu/libGL.so.1:/usr/lib/x86_64-linux-gnu/libGL.so.1:ro)
  _out+=(-v /usr/lib/x86_64-linux-gnu/libEGL.so.1:/usr/lib/x86_64-linux-gnu/libEGL.so.1:ro)

  if [ -n "$GID" ]; then
    _out+=(--group-add "${GID}")
  fi
  _out+=(--env HOST_PGID="$GID")
  _out+=(-v /etc/group:/etc/group:ro)

  # Bind full host nvidia libdir if present (keeps versions identical to host)
  if [ -n "${HOST_NVIDIA_LIB_DIR}" ] && [ -d "${HOST_NVIDIA_LIB_DIR}" ]; then
    _out+=(-v "${HOST_NVIDIA_LIB_DIR}:${HOST_NVIDIA_LIB_DIR}:ro")
    # ensure the container's runtime path prefers the host nvidia dir
    _out+=(-e LD_LIBRARY_PATH="${HOST_NVIDIA_LIB_DIR}:/usr/lib/x86_64-linux-gnu")
  fi

  # NVIDIA runtime / docker GPU flags
  _out+=(--runtime=nvidia)
  _out+=(--gpus all)
  _out+=(-e NVIDIA_VISIBLE_DEVICES=0)
  _out+=(-e NVIDIA_DRIVER_CAPABILITIES=all)

  # Force GLX vendor
  _out+=(-e __GLX_VENDOR_LIBRARY_NAME=nvidia)
  _out+=(-e LIBGL_ALWAYS_INDIRECT=0)

  # Append any user provided extra args (split safely)
  if [ -n "${EXTRA_DOCKER_ARGS}" ]; then
    # shellcheck disable=SC2206
    local -a extra_args=(${EXTRA_DOCKER_ARGS})
    _out+=("${extra_args[@]}")
  fi
}

function run_container_foreground() {
  local debug_mode=${1:-0}
  local shell_mode=${2:-0}
  echo "[run.sh] Starting container ${CONTAINER_NAME} from image ${IMAGE_NAME} (foreground)..."

  local DOCKER_RUN_ARGS
  base_docker_args_array DOCKER_RUN_ARGS

  # interactive TTY for foreground runs
  DOCKER_RUN_ARGS+=(-it)

  if [ "${debug_mode}" = "1" ]; then
    DOCKER_RUN_ARGS+=(-e K4A_LOG_LEVEL=TRACE)
  fi

  if [ "${shell_mode}" = "1" ]; then
    DOCKER_RUN_ARGS+=(--entrypoint /bin/bash)
  fi

  echo "docker run ${DOCKER_RUN_ARGS[*]} ${IMAGE_NAME}"
  docker run "${DOCKER_RUN_ARGS[@]}" "${IMAGE_NAME}"
}

function start_container_detached() {
  local debug_mode=${1:-0}
  local shell_mode=${2:-0}
  echo "[run.sh] Starting container ${CONTAINER_NAME} (detached) ..."
  local DOCKER_RUN_ARGS
  base_docker_args_array DOCKER_RUN_ARGS
  DOCKER_RUN_ARGS+=(--detach)

  if [ "${debug_mode}" = "1" ]; then
    DOCKER_RUN_ARGS+=(-e K4A_LOG_LEVEL=TRACE)
  fi

  if [ "${shell_mode}" = "1" ]; then
    # for detached shell mode keep entrypoint change but no -it
    DOCKER_RUN_ARGS+=(--entrypoint /bin/bash)
  fi

  docker run "${DOCKER_RUN_ARGS[@]}" "${IMAGE_NAME}"
}

case "${1:-}" in
  build)
    build_image
    ;;

  up)
    # parse optional flags for up: --debug and --shell (can be combined)
    DEBUG_FLAG=0
    SHELL_FLAG=0
    # shift arguments passed to up (if any)
    shift || true
    while [ "${1:-}" != "" ]; do
      case "$1" in
        --debug|-d)
          DEBUG_FLAG=1
          ;;
        --shell|--sh)
          SHELL_FLAG=1
          ;;
        *)
          echo "Unknown up option: $1"
          help_msg
          exit 2
          ;;
      esac
      shift || break
    done

    if [ "${BUILD_IF_MISSING}" = "1" ] && ! image_exists; then
      build_image
    fi
    stop_container_if_exists

    # allow local connections from container to X (least-privileged recommended variant)
    echo "[run.sh] Granting X access for local root user (host): xhost +SI:localuser:root"
    xhost +SI:localuser:root

    if [ -t 1 ]; then
      run_container_foreground "${DEBUG_FLAG}" "${SHELL_FLAG}"
    else
      start_container_detached "${DEBUG_FLAG}" "${SHELL_FLAG}"
      echo "[run.sh] Container started detached. Use './run.sh logs' to view logs."
    fi
    ;;

  down)
    stop_container_if_exists
    ;;

  status)
    docker ps -a --filter "name=^/${CONTAINER_NAME}$" --format "table {{.Names}}\t{{.Status}}\t{{.Image}}"
    ;;

  logs)
    if ! docker ps -a --format '{{.Names}}' | grep -x "${CONTAINER_NAME}" >/dev/null 2>&1; then
      echo "[run.sh] No container named ${CONTAINER_NAME} found."
      exit 1
    fi
    docker logs -f "${CONTAINER_NAME}"
    ;;

  ""|help|-h|--help)
    help_msg
    ;;

  *)
    echo "Unknown command: $1"
    help_msg
    exit 2
    ;;
esac
