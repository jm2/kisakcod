#!/usr/bin/env python3
"""Check that a Vulkan loader and ICD can present on macOS.

Loads the given loader with only the given ICD manifest and enumerates the
instance extensions. That needs no GPU, so it runs on a CI runner. The client's
SDL3 window needs VK_KHR_surface and VK_EXT_metal_surface from the driver; a
KosmicKrisp built without its macOS platform (-Dplatforms=) offers neither, and
the client then fails at startup on every Mac.

Usage: check_vulkan_wsi.py <libvulkan.1.dylib> <icd.json>
"""

import ctypes
import os
import sys

REQUIRED = ("VK_KHR_surface", "VK_EXT_metal_surface")


class VkExtensionProperties(ctypes.Structure):
    _fields_ = [("extensionName", ctypes.c_char * 256), ("specVersion", ctypes.c_uint32)]


def instance_extensions(loader_path):
    loader = ctypes.CDLL(loader_path)
    enumerate_extensions = loader.vkEnumerateInstanceExtensionProperties
    enumerate_extensions.restype = ctypes.c_int32
    enumerate_extensions.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_uint32),
                                     ctypes.POINTER(VkExtensionProperties)]
    count = ctypes.c_uint32(0)
    result = enumerate_extensions(None, ctypes.byref(count), None)
    if result != 0:
        raise RuntimeError(f"vkEnumerateInstanceExtensionProperties returned {result}")
    properties = (VkExtensionProperties * count.value)()
    result = enumerate_extensions(None, ctypes.byref(count), properties)
    if result != 0:
        raise RuntimeError(f"vkEnumerateInstanceExtensionProperties returned {result}")
    return {p.extensionName.decode() for p in properties[:count.value]}


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    loader_path, icd = argv[1], argv[2]
    for path in (loader_path, icd):
        if not os.path.isfile(path):
            print(f"check_vulkan_wsi: missing {path}", file=sys.stderr)
            return 1
    # Only this ICD, and no implicit layers from the host.
    os.environ["VK_DRIVER_FILES"] = os.path.abspath(icd)
    os.environ.pop("VK_ICD_FILENAMES", None)
    os.environ["VK_LOADER_LAYERS_DISABLE"] = "~all~"
    extensions = instance_extensions(loader_path)
    missing = [name for name in REQUIRED if name not in extensions]
    if missing:
        print(f"check_vulkan_wsi: {icd} offers no {', '.join(missing)} "
              f"({len(extensions)} instance extensions: {', '.join(sorted(extensions))})", file=sys.stderr)
        return 1
    print(f"check_vulkan_wsi: ok ({', '.join(REQUIRED)})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
