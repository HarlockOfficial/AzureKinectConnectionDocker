#include "CheckKinect.h"

bool is_kinect_device(libusb_device_descriptor const &d) {
    if (d.idVendor != 0x045e) return false; // Microsoft
    // check PIDs starting with 0x097 (adjust as needed)
    // Accept 0x0970..0x097f range
    return (d.idProduct & 0xfff0) == 0x0970;
}

struct FoundDev {
    libusb_device *dev;
    libusb_device_descriptor desc;
};

int check_kinect() {
    libusb_context *ctx = nullptr;
    int rc = libusb_init(&ctx);
    if (rc != LIBUSB_SUCCESS) {
        std::cerr << "libusb_init failed: " << libusb_error_name(rc) << "\n";
        return 2;
    }

    libusb_device **list = nullptr;
    ssize_t cnt = libusb_get_device_list(ctx, &list);
    if (cnt < 0) {
        std::cerr << "libusb_get_device_list failed\n";
        libusb_exit(ctx);
        return 2;
    }

    std::vector<FoundDev> found;
    for (ssize_t i = 0; i < cnt; ++i) {
        libusb_device *d = list[i];
        libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(d, &desc) != LIBUSB_SUCCESS) continue;
        if (is_kinect_device(desc)) {
            found.push_back({d, desc});
        }
    }

    if (found.empty()) {
        std::cerr << "No Kinect device found\n";
        libusb_free_device_list(list, 1);
        libusb_exit(ctx);
        return 1;
    }

    bool any_busy = false;
    for (auto &fd : found) {
        uint16_t vid = fd.desc.idVendor;
        uint16_t pid = fd.desc.idProduct;
        std::cout << "Found device VID=0x" << std::hex << vid << " PID=0x" << pid << std::dec << "\n";

        libusb_device_handle *handle = nullptr;
        int open_rc = libusb_open(fd.dev, &handle);
        if (open_rc == LIBUSB_SUCCESS) {
            std::cout << "libusb_open succeeded — device is available for use by this process\n";
            // optional: check kernel driver or claim interface safely if needed
            // Example: check kernel driver on interface 0
            int active = 0;
            int drv_rc = libusb_kernel_driver_active(handle, 0);
            if (drv_rc == 1) {
                std::cout << "Kernel driver active on interface 0\n";
            } else if (drv_rc == 0) {
                std::cout << "No kernel driver on interface 0\n";
            } else {
                std::cout << "libusb_kernel_driver_active returned " << drv_rc << "\n";
            }
            libusb_close(handle);
        } else {
            if (open_rc == LIBUSB_ERROR_BUSY) {
                std::cout << "Device is busy (already claimed by another process)\n";
                any_busy = true;
            } else {
                std::cout << "libusb_open failed: " << libusb_error_name(open_rc) << "\n";
            }
        }
    }

    libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return any_busy ? 3 : 0;
}
