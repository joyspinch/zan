#!/usr/bin/env bash
# Build the Zan-implemented runtime object (runtime_core.zan, batches 1-14)
# with the native bootstrap compiler and localize its non-API symbols.
#
#   SEED=/path/to/stage2 bash crt-transition/build_zan_core.sh [out.o]
#
# Produces an object exporting exactly the 152 allowlisted zan_* symbols
# (monotonic/sha256/sha512/alloc/free/crc32 + pkg_fopen/file_*/plat_*/
# io_* + atomic/audio/monitor/dispatch/eh/shared_table + io reactor
# co helpers + set_ready_hook/thread_start/thread_current_id + mem ops
# copy/fill/compare/find/get_string/put_string + UTF-16 text converters
# multi_byte_to_wide_char/wide_char_to_multi_byte + weakened embed_*;
# batch 14 adds md5/sha1/hmac_sha256/urandom/vm_read/lock_enter/
# lock_exit/exe_path — the target-agnostic faces the compiler's
# emitters and stdlib AppPath now call).
# Since batch 13 this object ALONE is the whole darwin-lane runtime (the
# -DZAN_RT_CORE_ZAN builds of zanstubs.c/zanhost.c are empty and kept
# only as the recorded C remainder):
#
#   python3 scripts/native_regression.py --seed "$SEED" \
#     --runtime "crt-transition/runtime_core.o" \
#     tests/selfhost/native_rt_core.zan
#
# The pure-C baseline for A/B regression is built alongside:
#   zanstubs_full.o + zanhost_full.o (both without the define).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SEED="${SEED:?Set SEED=/path/to/native-compiler-or-gen1}"
OUT="${1:-$ROOT/crt-transition/runtime_core.o}"

cd "$ROOT"
"$SEED" "$OUT" crt-transition/runtime_core.zan
python3 crt-transition/localize_syms.py "$OUT" \
  _zan_monotonic_ns _zan_monotonic_us _zan_sha256 _zan_sha512 \
  _zan_alloc _zan_free _zan_crc32 \
  _zan_pkg_fopen _zan_file_fopen _zan_file_remove _zan_file_rename \
  _zan_file_read_path \
  _zan_file_open _zan_file_read _zan_file_write _zan_file_seek \
  _zan_file_tell _zan_file_flush _zan_file_close _zan_file_eof \
  _zan_file_length _zan_file_attributes _zan_file_time \
  _zan_file_set_readonly _zan_file_set_time \
  _zan_file_try_lock _zan_file_unlock \
  _zan_mmap_create _zan_mmap_open _zan_mmap_from_file _zan_mmap_map \
  _zan_mmap_unmap _zan_mmap_flush _zan_mmap_close _zan_mmap_unlink \
  _zan_plat_net_interfaces _zan_plat_icmp_ping \
  _zan_io_socket_cleanup _zan_io_socket_send _zan_io_socket_recv \
  _zan_io_sockaddr_ip_str _zan_io_socket_peer_ip _zan_io_socket_peer_ip_into \
  _zan_io_sockaddr_ip_str_into _zan_io_resolve_sa _zan_io_resolve_ipv4 \
  _zan_io_sockaddr_family _zan_io_sockaddr_is_safe _zan_io_resolve_all \
  _zan_io_resolve_all_async _zan_io_connect_sa _zan_io_connect_status \
  _zan_io_socket_ready _zan_io_socket_alive \
  _zan_atomic_int_create _zan_atomic_int_destroy _zan_atomic_int_load \
  _zan_atomic_int_store _zan_atomic_int_add _zan_atomic_int_exchange \
  _zan_atomic_int_compare_exchange \
  _zan_audio_open _zan_audio_close _zan_audio_is_open \
  _zan_audio_driver_name _zan_audio_last_error _zan_audio_volume \
  _zan_audio_set_volume _zan_audio_active_voices _zan_audio_stop_all \
  _zan_audio_load_wav _zan_audio_load_ogg _zan_audio_load_wav_mem \
  _zan_audio_load_ogg_mem _zan_audio_free_clip _zan_audio_clip_duration_ms \
  _zan_audio_clip_channels _zan_audio_clip_frequency _zan_audio_play \
  _zan_audio_voice_playing _zan_audio_voice_stop _zan_audio_voice_set_gain \
  _zan_monitor_enter _zan_monitor_exit \
  _zan_dispatch_init _zan_dispatch_post _zan_dispatch_take _zan_dispatch_clear \
  _zan_eh_tls_state \
  _zan_shared_table_create _zan_shared_table_create_anon _zan_shared_table_open \
  _zan_shared_table_handle _zan_shared_table_attach _zan_shared_table_close \
  _zan_shared_table_destroy _zan_shared_table_set_int _zan_shared_table_get_int \
  _zan_shared_table_set_float _zan_shared_table_get_float \
  _zan_shared_table_set_string _zan_shared_table_get_string \
  _zan_shared_table_increment _zan_shared_table_expire \
  _zan_shared_table_expire_at _zan_shared_table_expires_at \
  _zan_shared_table_purge_expired _zan_shared_table_rate_allow \
  _zan_shared_table_lock_acquire _zan_shared_table_lock_release \
  _zan_shared_table_delete _zan_shared_table_exists _zan_shared_table_count \
  _zan_shared_table_clear _zan_shared_table_hash _zan_shared_table_stat \
  _zan_shared_table_set_int_at _zan_shared_table_get_int_at \
  _zan_shared_table_increment_at _zan_shared_table_extreme_at \
  _zan_shared_table_set_string_at _zan_shared_table_get_string_at \
  _zan_shared_table_match_at _zan_shared_table_exists_at \
  _zan_shared_table_delete_at \
  _zan_io_wait_co _zan_io_recv_co _zan_io_recv_to_co _zan_io_accept_co \
  _zan_io_poll _zan_io_close_notify _zan_resolve_sa_co _zan_resolve_ipv4_co \
  _zan_set_ready_hook _zan_thread_start _zan_thread_current_id \
  _zan_copy _zan_fill _zan_compare _zan_find \
  _zan_get_string _zan_put_string \
  _zan_multi_byte_to_wide_char _zan_wide_char_to_multi_byte \
  _zan_md5 _zan_sha1 _zan_hmac_sha256 _zan_urandom _zan_vm_read \
  _zan_lock_enter _zan_lock_exit _zan_exe_path \
  --weaken \
  _zan_embed_has _zan_embed_read _zan_embed_bytes _zan_embed_list
cc -DZAN_RT_CORE_ZAN \
  -I/Users/qq/Desktop/zanlang/zan-lang/src/common \
  -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZIP_APIS -DMINIZ_NO_STDIO \
  -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITERS \
  -c -o "$ROOT/crt-transition/zanstubs_rest.o" \
  "$ROOT/crt-transition/zanstubs.c"
cc -DZAN_RT_CORE_ZAN -c -o "$ROOT/crt-transition/zanhost_rest.o" \
  "$ROOT/crt-transition/zanhost.c"
cc -c -o "$ROOT/crt-transition/zanstubs_full.o" "$ROOT/crt-transition/zanstubs.c"
cc -c -o "$ROOT/crt-transition/zanhost_full.o" "$ROOT/crt-transition/zanhost.c"
printf 'Zan runtime object: %s\nC remainder:        %s %s\nC baseline:         %s %s\n' \
  "$OUT" \
  "$ROOT/crt-transition/zanstubs_rest.o" "$ROOT/crt-transition/zanhost_rest.o" \
  "$ROOT/crt-transition/zanstubs_full.o" "$ROOT/crt-transition/zanhost_full.o"
