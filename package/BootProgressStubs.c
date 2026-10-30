/*
 * Weak definitions for the counters UniXNU-sheng's diagnostic renderer reports next to the
 * UFS rows. This board carries no Virtio transport, no verified-block chain, no Synaptics
 * touch, no legacy display, no Adreno and no sheng PMGR, and the kernel link does not honour
 * weak_import, so every symbol the renderer mentions has to exist. They are weak: a strong
 * definition from a component still wins, and the page shows zeros for rows this board cannot
 * fill. Generated from the extern declarations of package/BootProgress.c.
 */
#include <stdint.h>
#include <stddef.h>

volatile uint64_t native_ufs_verified_blocks __attribute__((weak)) = 0;
volatile uint64_t native_ufs_verify_ticks __attribute__((weak)) = 0;
volatile uint64_t native_ufs_read_ticks __attribute__((weak)) = 0;
volatile uint32_t native_ufs_probe_calls __attribute__((weak)) = 0;
volatile uint32_t native_ufs_matched __attribute__((weak)) = 0;
const char *volatile native_ufs_root_failure __attribute__((weak)) = 0;
volatile uint32_t native_virtio_ready __attribute__((weak)) = 0;
volatile uint32_t native_virtio_read_requests __attribute__((weak)) = 0;
volatile uint32_t native_virtio_read_failures __attribute__((weak)) = 0;
volatile uint64_t native_virtio_verified_blocks __attribute__((weak)) = 0;
volatile uint32_t native_ram_block_sparse __attribute__((weak)) = 0;
volatile uint64_t native_ram_block_bytes __attribute__((weak)) = 0;
volatile uint64_t native_ram_block_resident_bytes __attribute__((weak)) = 0;
__attribute__((weak)) void sheng_cpufreq_poll(void) { }
char native_touch_rows[4][120] __attribute__((weak));
char native_cpufreq_row[120] __attribute__((weak));
volatile uint32_t native_hid_input_events[8] __attribute__((weak));
volatile int native_hid_input_results[8] __attribute__((weak));
volatile uint32_t native_touch_loop_step __attribute__((weak)) = 0;
volatile uint32_t native_touch_seq __attribute__((weak)) = 0;
volatile uint32_t native_touch_count __attribute__((weak)) = 0;
volatile int native_touch_xs[12] __attribute__((weak));
volatile int native_touch_ys[12] __attribute__((weak));
volatile int native_touch_ids[12] __attribute__((weak));
volatile uint32_t native_cpufreq_stage __attribute__((weak)) = 0;
volatile unsigned native_touch_stage_req __attribute__((weak)) = 0;
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
volatile uint32_t native_legacy_rej_options[3] __attribute__((weak));
volatile uint32_t native_legacy_rej_size[2] __attribute__((weak));
volatile uint32_t native_legacy_rej_rects[2][4] __attribute__((weak));
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
volatile uint64_t native_data_persist_pages __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_loaded __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_writes __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_syncs __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_errors __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_first_lba __attribute__((weak)) = 0;
volatile uint64_t native_data_persist_present __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_n __attribute__((weak)) = 0;
volatile uint64_t native_gop_gpu_offers __attribute__((weak)) = 0;
volatile uint64_t native_gop_gpu_completions __attribute__((weak)) = 0;
volatile uint64_t native_gop_gpu_failures __attribute__((weak)) = 0;
volatile uint64_t native_gop_gpu_abandoned __attribute__((weak)) = 0;
volatile uint32_t native_legacy_gpu_fallbacks __attribute__((weak)) = 0;
volatile uint32_t native_legacy_gpu_last_status __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_last __attribute__((weak)) = 0;
volatile uint64_t native_legacy_early_retires __attribute__((weak)) = 0;
volatile uint64_t native_legacy_early_failures __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_max __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_sum __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_bytes_last __attribute__((weak)) = 0;
volatile uint64_t native_legacy_copy_bytes_sum __attribute__((weak)) = 0;
char native_wi_log[4][100] __attribute__((weak));
volatile unsigned native_wi_count __attribute__((weak)) = 0;
volatile uint64_t native_adreno_cache_stats[5] __attribute__((weak));
volatile uint32_t native_adreno_coherence[8] __attribute__((weak));
volatile uint32_t native_adreno_wc_distrusted __attribute__((weak)) = 0;
volatile uint32_t native_adreno_wc_audit[6] __attribute__((weak));
volatile uint64_t native_adreno_reassert_ticks __attribute__((weak)) = 0;
volatile uint64_t native_adreno_audit_ticks __attribute__((weak)) = 0;
volatile uint32_t native_adreno_audit_attr __attribute__((weak)) = 0;
