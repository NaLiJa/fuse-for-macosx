#include "config.h"

#include "xfs.h"
#include "xfs_engines.h"
#include "xfs_worker.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#if defined(WIN32) || defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "libspectrum.h"
#include "../spectranet.h"
#include "memory_pages.h"
#include "ui/ui.h"
#include "compat.h"
#include "utils.h"

volatile struct xfs_registers_t xfs_registers = {};

// XFS debug logging control
static bool xfs_debug_enabled = false;

// Macro for XFS debug output (only prints if enabled)
#define XFS_DEBUG(...) do { if (xfs_debug_enabled) printf(__VA_ARGS__); } while(0)

// Function to enable/disable XFS debug logging
void xfs_debug_enable(bool enable)
{
    xfs_debug_enabled = enable;
}

bool xfs_debug_is_enabled(void)
{
    return xfs_debug_enabled;
}

// XFS base directory path
char xfs_base_path[512];

static int xfs_copy_spectranext_launcher_file(const char *filename)
{
    char source_name[PATH_MAX];
    char destination_path[PATH_MAX];
    utils_file source_file;

    if (snprintf(source_name, sizeof(source_name), "spectranext-launcher/%s", filename) >=
        (int)sizeof(source_name))
        return 1;

    if (snprintf(destination_path, sizeof(destination_path), "%s" FUSE_DIR_SEP_STR "%s",
                 xfs_base_path, filename) >= (int)sizeof(destination_path))
        return 1;

    if (utils_read_auxiliary_file(source_name, &source_file, UTILS_AUXILIARY_ROM) != 0 &&
        utils_read_auxiliary_file(filename, &source_file, UTILS_AUXILIARY_ROM) != 0)
        return 1;

    const int error = utils_write_file(destination_path, source_file.buffer, source_file.length);
    utils_close_file(&source_file);
    return error;
}

static void xfs_seed_spectranext_launcher_files(void)
{
    char boot_path[PATH_MAX];

    if (spectranext_reboot_check_launcher_suppressed())
        return;

    if (snprintf(boot_path, sizeof(boot_path), "%s" FUSE_DIR_SEP_STR "boot.zx", xfs_base_path) >=
        (int)sizeof(boot_path))
        return;
  
    uint8_t auto_boot = 0;

    spectranet_config_get_byte(
        CONFIG_SECTION_AUTO_MOUNT,
        CONFIG_ITEM_AUTO_BOOT,
        &auto_boot
    );
  
    if (auto_boot == 0)
        return;
  
    /**
     RAMFS boot behavior
     When xfs://ram/ is selected as a mount point 0 and auto_boot is enabled,
     Spectranet will attempt to load "boot.zx" file. This code presents that file with launcher code.
     However, when user has uploaded custom boot.zx we must take care not to overwrite that file.
     */
  
    char mount[128];
    if (spectranet_config_get_string(CONFIG_SECTION_AUTO_MOUNT,
                                     CONFIG_ITEM_MOUNT_RESOURCE,
                                     mount, sizeof(mount)) == 0)
    {
        if (strcmp("xfs://ram/", mount) == 0)
        {
            xfs_copy_spectranext_launcher_file("boot.zx");
            xfs_copy_spectranext_launcher_file("launcher.bin");
        }
    }
}

void xfs_init()
{
    snprintf(xfs_base_path, sizeof(xfs_base_path), "%s/xfs", compat_get_config_path());
    
    // Create "xfs" directory in Application Support directory
#if defined(WIN32) || defined(_WIN32)
    if (_mkdir(xfs_base_path) != 0 && errno != EEXIST)
#else
    if (mkdir(xfs_base_path, 0755) != 0 && errno != EEXIST)
#endif
    {
        ui_error( UI_ERROR_WARNING, "xfs: failed to create xfs directory: %s\n", strerror(errno) );
    }
    
    xfs_seed_spectranext_launcher_files();
    XFS_DEBUG("xfs: initialized with base path: %s\n", xfs_base_path ? xfs_base_path : "(null)");
}

void xfs_reset(void)
{
    XFS_DEBUG("xfs: reset - cleaning up all mounts and handles\n");
    
    // Call xfs_free() from xfs.c to clean up all resources
    xfs_free();
    
    XFS_DEBUG("xfs: reset complete\n");
}

libspectrum_byte xfs_read( memory_page *page GCC_UNUSED, libspectrum_word address )
{
    libspectrum_word offset = address & 0xfff;  // XFS is single page (0x49)
    uint8_t *registers = (uint8_t*)&xfs_registers;
    return registers[offset];
}

void xfs_write( memory_page *page GCC_UNUSED, libspectrum_word address, libspectrum_byte b )
{
    libspectrum_word offset = address & 0xfff;
    uint8_t *registers = (uint8_t*)&xfs_registers;
    registers[offset] = b;
    
    if (offset == 0)
    {
        // Command register written - process command via dispatcher
        xfs_handle_command(&xfs_registers);
    }
}
