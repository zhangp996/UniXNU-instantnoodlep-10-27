// SPDX-License-Identifier: BSD-2-Clause-Patent
// Board hooks for the USB debug console.
//
// boot_progress_persist and boot_reset_bootloader are what the console calls. The report below is
// this port's own diagnostic: the console's poll worker leaves silently when its controller
// hand-off is refused, and the panel cannot show the reason otherwise.

#include <stdint.h>
#include <pexpert/pexpert.h>
#include <kern/debug.h>
#include <QcomXnuUsbDebug.h>

void boot_progress_persist (void)
{
    // Nothing records boot progress on this board yet; the console keeps its own ring buffer.
}

void boot_reset_bootloader (void)
{
    register uint64_t function __asm__ ("x0") = 0x84000009ULL;   // PSCI SYSTEM_RESET
    __asm__ volatile ("smc #0" :: "r" (function) : "memory");
    for (;;) {
    }
}

void qcom_usb_board_check (void)
{
    struct qcom_usb_debug_status status;
    int report = 0;
    qcom_usb_debug_get_status (&status);
    if (status.phase == QCOM_USB_DEBUG_ACTIVE) {
        kprintf ("QCOM USB DEBUG active\n");
        return;
    }
    if (!PE_parse_boot_argn ("qcom-usb-report", &report, sizeof (report)) || report != 1) {
        kprintf ("QCOM USB DEBUG refused: phase=%u error=%u\n", status.phase, status.error);
        return;
    }
    panic ("QCOM USB DEBUG refused\n"
           "phase=%u error=%u core=%d qcom=%u restore=%u dma_mode=%u diag=%u\n"
           "vbus=%u/%u phy=%u/%u applied=%u hwparams0=%08x width=%u",
           status.phase, status.error, status.core_result, status.qcom_result, status.restore_result,
           status.dma_mode, status.diag_checkpoint,
           status.vbus_owned, status.vbus_saved, status.phy_owned, status.phy_saved, status.phy_applied,
           status.hwparams0, status.dma_address_width);
}
