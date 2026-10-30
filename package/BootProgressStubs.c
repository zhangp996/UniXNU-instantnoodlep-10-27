/*
 * Weak definitions for the board-local names UniXNU-sheng's diagnostic renderer reports but
 * this board does not provide: the Virtio transport, the verified-block chain, the Synaptics
 * touch rows, the legacy display, Adreno, the sheng PMGR cpu-frequency poll, the UI-trace and
 * boot-process probes and the SMP/IRQ counters. The kernel link does not honour weak_import,
 * so a name the renderer mentions must exist somewhere.
 *
 * They are weak on purpose: a strong definition from the kernel or a component still wins, so
 * the KonaUfs counters keep reporting and only the rows this board cannot fill read zero.
 * The boot_progress_* API and native_ufs_progress_text are deliberately absent - those must
 * come from the kernel patch and the KonaUfs component, and a stub would hide their absence.
 * Generated from the extern declarations of package/BootProgress.c.
 */
#include <stdint.h>
#include <stddef.h>

volatile uint64_t native_ufs_block_reads __attribute__((weak)) = 0;
volatile uint64_t native_ufs_verify_ticks __attribute__((weak)) = 0;
volatile uint64_t native_ufs_block_failures __attribute__((weak)) = 0;
volatile uint32_t native_ufs_integrity_failures __attribute__((weak)) = 0;
volatile uint32_t native_ufs_probe_calls __attribute__((weak)) = 0;
volatile uint32_t native_ufs_root_step __attribute__((weak)) = 0;
const char *volatile native_ufs_root_failure __attribute__((weak)) = 0;
volatile uint32_t native_virtio_ready __attribute__((weak)) = 0;
volatile uint64_t native_virtio_verified_blocks __attribute__((weak)) = 0;
volatile uint32_t native_ram_block_sparse __attribute__((weak)) = 0;
volatile uint64_t native_ram_block_bytes __attribute__((weak)) = 0;
__attribute__((weak)) void boot_ui_trace_snapshot(unsigned boot_ui_trace_snapshot0) { (void)boot_ui_trace_snapshot0; }
char boot_ui_trace_rows[14][120] __attribute__((weak));
char boot_ui_trace_label[120] __attribute__((weak));
unsigned boot_ui_trace_ready __attribute__((weak)) = 0;
volatile unsigned boot_ui_trace_phase __attribute__((weak)) = 0;
volatile unsigned boot_ui_trace_index __attribute__((weak)) = 0;
__attribute__((weak)) void boot_process_capture() { }
__attribute__((weak)) void boot_process_snapshot() { }
__attribute__((weak)) void boot_log_tick(unsigned boot_log_tick0) { (void)boot_log_tick0; }
__attribute__((weak)) void sheng_cpufreq_poll() { }
char boot_process_rows[10][120] __attribute__((weak));
__attribute__((weak)) unsigned boot_ui_trace_role_rows(unsigned boot_ui_trace_role_rows0, char boot_ui_trace_role_rows1[][120], unsigned boot_ui_trace_role_rows2, unsigned boot_ui_trace_role_rows3) { (void)boot_ui_trace_role_rows0; (void)boot_ui_trace_role_rows1; (void)boot_ui_trace_role_rows2; (void)boot_ui_trace_role_rows3; return 0; }
char native_touch_rows[4][120] __attribute__((weak));
char native_cpufreq_row[120] __attribute__((weak));
volatile uint32_t native_hid_input_events[8] __attribute__((weak));
volatile int native_hid_input_results[8] __attribute__((weak));
volatile uint32_t native_touch_seq __attribute__((weak)) = 0;
volatile int native_touch_xs[12] __attribute__((weak));
volatile uint32_t native_cpufreq_stage __attribute__((weak)) = 0;
volatile unsigned native_touch_stage_req __attribute__((weak)) = 0;
volatile unsigned boot_process_passes __attribute__((weak)) = 0;
volatile unsigned boot_process_attempts __attribute__((weak)) = 0;
volatile unsigned boot_process_busy __attribute__((weak)) = 0;
volatile uint32_t armcpu_smp_requested __attribute__((weak)) = 0;
volatile uint32_t armcpu_smp_step[8] __attribute__((weak));
volatile int32_t armcpu_smp_psci_rc[8] __attribute__((weak));
volatile uint32_t armcpu_irq_phase __attribute__((weak)) = 0;
volatile int native_owner_root_accepted __attribute__((weak)) = 0;
volatile uint32_t native_legacy_stage __attribute__((weak)) = 0;
volatile uint32_t native_legacy_pm_stage __attribute__((weak)) = 0;
volatile uint32_t native_legacy_pm_calls __attribute__((weak)) = 0;
volatile uint32_t native_legacy_pm_callbacks __attribute__((weak)) = 0;
volatile uint32_t native_legacy_swap_validation_calls __attribute__((weak)) = 0;
volatile uint32_t native_legacy_swap_validation_result __attribute__((weak)) = 0;
volatile uint32_t native_legacy_swap_validation_reason __attribute__((weak)) = 0;
volatile uint32_t native_legacy_swap_accepted_count __attribute__((weak)) = 0;
volatile uint32_t native_legacy_swap_rejected_count __attribute__((weak)) = 0;
volatile uint64_t native_legacy_rej_surface[7] __attribute__((weak));
volatile uint32_t native_legacy_rej_reason __attribute__((weak)) = 0;
volatile uint64_t native_legacy_rej_surfaces[4] __attribute__((weak));
volatile uint32_t native_legacy_rej_state[3] __attribute__((weak));
volatile uint32_t native_legacy_rej_size[2] __attribute__((weak));
char native_legacy_rej_class[32] __attribute__((weak));
volatile uint32_t native_legacy_frame_count __attribute__((weak)) = 0;
volatile uint32_t native_legacy_frame_kind[32] __attribute__((weak));
volatile uint32_t native_legacy_frame_time[32] __attribute__((weak));
volatile uint64_t native_legacy_frame_stats[32][5] __attribute__((weak));
volatile uint32_t native_legacy_frame_colors[8][4] __attribute__((weak));
volatile uint64_t native_legacy_frame_color_counts[8][4] __attribute__((weak));
volatile uint64_t native_legacy_frame_color_other[8] __attribute__((weak));
volatile uint32_t native_legacy_transfer_calls __attribute__((weak)) = 0;
volatile uint32_t native_legacy_retirement_calls __attribute__((weak)) = 0;
volatile uint32_t native_legacy_guard_slot __attribute__((weak)) = 0;
volatile uint32_t native_legacy_prepare_calls __attribute__((weak)) = 0;
volatile uint32_t native_legacy_prepare_reason __attribute__((weak)) = 0;
volatile uint32_t native_legacy_prepare_attempts __attribute__((weak)) = 0;
volatile uint32_t native_legacy_client_calls __attribute__((weak)) = 0;
volatile uint32_t native_legacy_client_type __attribute__((weak)) = 0;
volatile int native_legacy_client_pid __attribute__((weak)) = 0;
volatile int native_legacy_client_status __attribute__((weak)) = 0;
char native_legacy_client_class[64] __attribute__((weak));
volatile uint64_t native_legacy_gop_virtual __attribute__((weak)) = 0;
volatile uint64_t native_legacy_gop_physical __attribute__((weak)) = 0;
volatile uint64_t native_legacy_gop_bytes __attribute__((weak)) = 0;
volatile int native_legacy_transfer_status __attribute__((weak)) = 0;
volatile uint64_t native_legacy_transfer_bytes __attribute__((weak)) = 0;
volatile uint32_t native_data_persist_state __attribute__((weak)) = 0;
volatile uint32_t native_data_persist_header __attribute__((weak)) = 0;
volatile uint32_t native_data_persist_gpt __attribute__((weak)) = 0;
volatile uint32_t native_data_persist_gpt_after __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_loaded __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_writes __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_syncs __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_errors __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_first_lba __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_present __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_n __attribute__((weak)) = 0;
volatile uint64_t native_gop_gpu_offers __attribute__((weak)) = 0;
volatile uint32_t native_legacy_gpu_fallbacks __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_last __attribute__((weak)) = 0;
volatile uint64_t native_legacy_early_retires __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_max __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_sum __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_bytes_last __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_bytes_sum __attribute__((weak)) = 0;
char native_wi_log[4][100] __attribute__((weak));
volatile unsigned native_wi_count __attribute__((weak)) = 0;
volatile uint64_t armcpu_timer_interrupts __attribute__((weak)) = 0;
volatile uint64_t native_adreno_cache_stats[5] __attribute__((weak));
volatile uint32_t native_adreno_coherence[8] __attribute__((weak));
volatile uint32_t native_adreno_wc_distrusted __attribute__((weak)) = 0;
volatile uint32_t native_adreno_wc_audit[6] __attribute__((weak));
volatile uint64_t native_adreno_reassert_ticks __attribute__((weak)) = 0;
volatile uint64_t native_adreno_audit_ticks __attribute__((weak)) = 0;
volatile uint32_t native_adreno_audit_attr __attribute__((weak)) = 0;
volatile uint64_t armcpu_ipi_interrupts __attribute__((weak)) = 0;
__attribute__((weak)) uint64_t boot_diag_scan(uint64_t boot_diag_scan0) { (void)boot_diag_scan0; return 0; }
