/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2020 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

/**
 * Static definitions for statistics.
 *
 * Requires STAT, CBSTAT, and LABEL macros be defined before including this
 * file. LABEL wraps a key and a value. A common definition for LABEL would be
 *
 * LABEL(key, value) {#key, #value}
 *
 * to stringify for insertion into a map.
 *
 * STAT(enumKey, cbstatName, unit, familyName, ...)
 * CBSTAT(enumKey, cbstatName, [unit])
 *
 * where:
 *  * enumKey - a key which identifies the stat
 *  * cbstatName - key to expose this stat under for cbstats (defaults to
 *                 enumKey if empty)
 *  * unit - name of a cb::stats::Unit which identifies what unit the stat
 *           value represents (e.g., microseconds). default: none
 *  * familyName - the metric name used by Prometheus. This need _not_ be
 * unique, and can be shared by stats which are distinguishable by labels.
 * may be empty.
 * default: uniqueName
 * and the remaining optional parameters are 0 or more instances of
 *  * LABEL(key, value)
 *
 * Stats should be formatted as
 *
 * STAT(unique_name, , unit, family_name, [label]...)
 *
 * e.g.,
 *
 * STAT(get_cmd, , microseconds, cmd_time_taken, LABEL(op, get),
 *                                             LABEL(result, hit))
 * STAT(set_cmd, , microseconds, cmd_time_taken, LABEL(op, set),
 *                                             LABEL(result, miss))
 *
 * The uniqueName will be used as an enum key, and as the stat key for
 * backends which do not support labelled stat families (e.g., CBStats).
 *
 * The familyName and labels will be used by backends which _do_ support
 * them, like Prometheus. All stats of a given family_name should be of the same
 * unit (e.g., count, bytes, seconds, kilobytes per microsecond) and it should
 * be possible to meaningfully aggregate the stat values e.g., get_cmd and
 * set_cmd can be summed.
 *
 * Only uniqueName is mandatory. The minimal definition of a stat is therefore
 * STAT(uniqueName, , , )
 * For stats with unspecified units and no labels. In this case, the uniqueName
 * will also be used as the familyName.
 *
 * For stats which should _not_ be exposed to Prometheus, the CBSTAT macro
 * can be used instead. Only a unique name is required.
 *
 *  CBSTAT(uniqueName)
 *
 * Units are optional, and only informative - CBStats does not use units.
 *
 *  CBSTAT(uptime, , milliseconds)
 *
 */

#ifndef STAT
#warning A STAT macro must be defined before including stats.def.h
// If this header is being checked by clang-tidy or similar _not_ as part of
// definitions.h, there will not be a STAT definition.
// Define an empty STAT macro to avoid unnecessary warnings.
#define STAT(...)
#endif

#ifndef CBSTAT
#warning A CBSTAT macro must be defined before including stats.def.h
#define CBSTAT(...)
#endif

#ifndef LABEL
#warning A LABEL macro must be defined before including stats.def.h
#define LABEL(...)
#endif

// default_engine stats
STAT(default_evictions, "evictions", count, memcache_evictions, )
STAT(default_curr_items, "curr_items", count, memcache_curr_items, )
STAT(default_total_items, "total_items", count, memcache_total_items, )
STAT(default_bytes, "bytes", bytes, memcache_mem_size, )
STAT(default_reclaimed, "reclaimed", count, memcache_reclaimed, )
STAT(default_engine_maxbytes,
     "engine_maxbytes",
     count,
     memcache_engine_maxbytes, )

// include generated config STAT declarations
#include "stats_config.def.h" // NOLINT(*)

// TODO: applying a "kv_" prefix globally would be consistent but lead to kv_ep_
//  for some stats. Providing metric family names without ep_ would avoid this
// "All" stats group (doEngineStats)
STAT(ep_storage_age, , microseconds, , )
STAT(ep_storage_age_highwat, , microseconds, , )
STAT(ep_num_workers, , count, , )
STAT(ep_bucket_priority,
     ,
     none,
     , ) // TODO: make 0/1 rather than text for Prometheus?
STAT(ep_total_enqueued, , count, , )
STAT(ep_total_deduplicated, , count, , )
STAT(ep_expired_access, , count, , )
STAT(ep_expired_compactor, , count, , )
STAT(ep_expired_pager, , count, , )
STAT(ep_queue_size, , count, , )
STAT(ep_diskqueue_items, , count, , )
STAT(ep_commit_num, , count, , )
STAT(ep_commit_time, , microseconds, , )
STAT(ep_commit_time_total, , microseconds, , )
STAT(ep_item_begin_failed, , count, , )
STAT(ep_item_commit_failed, , count, , )
STAT(ep_item_flush_expired, , count, , )
STAT(ep_item_flush_failed, , count, , )
STAT(ep_flusher_state, , none, , )
STAT(ep_flusher_todo, , count, , )
STAT(ep_total_persisted, , count, , )
STAT(ep_uncommitted_items, , count, , )
STAT(ep_chk_persistence_timeout, , seconds, , )
STAT(ep_vbucket_del, , count, , )
STAT(ep_vbucket_del_fail, , count, , )
STAT(ep_flush_duration_total, , milliseconds, , )
STAT(ep_persist_vbstate_total, , count, , )
STAT(mem_used, , bytes, , )
STAT(mem_used_estimate, , bytes, , )
STAT(ep_mem_low_wat_percent, , percent, , )
STAT(ep_mem_high_wat_percent, , percent, , )
/* TODO: it's not advised to have metric like:
 *   my_metric{label=a} 1
 *   my_metric{label=b} 6
 *   my_metric{label=total} 7
 *  as a total is inconvenient for aggregation, _but_ we do track
 * several stats which are logically totals which might include things _not_
 * available under any other metric. Exposing it under a different metric name
 * seems best. Note: "..._total" is expected to be reserved for Counters -
 * totals over time, not totals of other things.
 */
STAT(bytes, , bytes, total_memory_used, )
STAT(ep_kv_size, , bytes, memory_used, LABEL(for, hashtable))
STAT(ep_blob_num, , count, , )
STAT(ep_blob_overhead, ,
     bytes,
     memory_overhead,
     LABEL(for, blobs)) // TODO: Assess what labels would actually be _useful_ for querying
STAT(ep_value_size, ,
     bytes,
     memory_used,
     LABEL(for, blobs))
STAT(ep_storedval_size, , bytes, memory_used, LABEL(for, storedvalues))
STAT(ep_storedval_overhead, , bytes, memory_overhead, LABEL(for, storedvalues))
STAT(ep_storedval_num, , count, , )
STAT(ep_overhead, , bytes, total_memory_overhead, )
STAT(ep_item_num, , count, , )
STAT(ep_oom_errors, , count, , )
STAT(ep_tmp_oom_errors, , count, , )
STAT(ep_mem_tracker_enabled, , none, , )
STAT(ep_bg_fetched, , count, , )
STAT(ep_bg_meta_fetched, , count, , )
STAT(ep_bg_remaining_items, , count, , )
STAT(ep_bg_remaining_jobs, , count, , )
STAT(ep_num_pager_runs, , count, , )
STAT(ep_num_expiry_pager_runs, , count, , )
STAT(ep_num_freq_decayer_runs, , count, , )
STAT(ep_items_expelled_from_checkpoints, , count, , )
STAT(ep_items_rm_from_checkpoints, , count, , )
STAT(ep_num_value_ejects, , count, , )
STAT(ep_num_eject_failures, , count, , )
STAT(ep_num_not_my_vbuckets, , count, , )
STAT(ep_pending_ops, , count, , )
STAT(ep_pending_ops_total,
     ,
     count,
     , ) // TODO: are total-over-uptime stats relevant for prometheus
         //  given the ability to sum over a time period?
STAT(ep_pending_ops_max,
     ,
     count,
     , ) // TODO: standardise labelling for "high watermark" style stats
STAT(ep_pending_ops_max_duration, , microseconds, , )
STAT(ep_pending_compactions, , count, , )
STAT(ep_rollback_count, , count, , )
STAT(ep_vbucket_del_max_walltime, , microseconds, , )
STAT(ep_vbucket_del_avg_walltime, , microseconds, , )
STAT(ep_bg_num_samples, , count, , )
STAT(ep_bg_min_wait, , microseconds, , )
STAT(ep_bg_max_wait, , microseconds, , )
STAT(ep_bg_wait_avg, , microseconds, , ) // TODO: derived from two stats. Decide
                                         //  whether to expose for prometheus
STAT(ep_bg_min_load, , microseconds, , )
STAT(ep_bg_max_load, , microseconds, , )
STAT(ep_bg_load_avg, , microseconds, , ) // TODO: derived from two stats. Decide
                                         //  whether to expose for prometheus
STAT(ep_bg_wait, , microseconds, , )
STAT(ep_bg_load, , microseconds, , )
STAT(ep_degraded_mode, , none, , )
STAT(ep_num_access_scanner_runs, , count, , )
STAT(ep_num_access_scanner_skips, , count, , )
STAT(ep_access_scanner_last_runtime,
     ,
     seconds,
     , ) // TODO: relative to server start. Convert to absolute time?
STAT(ep_access_scanner_num_items, , count, , )
STAT(ep_access_scanner_task_time,
     ,
     none,
     , ) // TODO: this is a string, expose numeric time for Prometheus
STAT(ep_expiry_pager_task_time,
     ,
     none,
     , ) // TODO: this is a string, expose numeric time for Prometheus
STAT(ep_startup_time, , seconds, , )
STAT(ep_warmup_thread, , none, , )
STAT(ep_warmup_time, , microseconds, , )
STAT(ep_warmup_oom, , count, , )
STAT(ep_warmup_dups, , count, , )
STAT(ep_num_ops_get_meta, , count, ops, LABEL(op, get_meta))
STAT(ep_num_ops_set_meta, , count, ops, LABEL(op, set_meta))
STAT(ep_num_ops_del_meta, , count, ops, LABEL(op, del_meta))
STAT(ep_num_ops_set_meta_res_fail, , count, ops_failed, LABEL(op, set_meta))
STAT(ep_num_ops_del_meta_res_fail, , count, ops_failed, LABEL(op, del_meta))
STAT(ep_num_ops_set_ret_meta, , count, ops, LABEL(op, set_ret_meta))
STAT(ep_num_ops_del_ret_meta, , count, ops, LABEL(op, del_ret_meta))
STAT(ep_num_ops_get_meta_on_set_meta,
     ,
     count,
     ops,
     LABEL(op, get_meta_for_set_meta))
STAT(ep_workload_pattern, , none, , )
STAT(ep_defragmenter_num_visited, , count, , )
STAT(ep_defragmenter_num_moved, , count, , )
STAT(ep_defragmenter_sv_num_moved, , count, , )
STAT(ep_item_compressor_num_visited, , count, , )
STAT(ep_item_compressor_num_compressed, , count, , )
STAT(ep_cursor_dropping_lower_threshold, , bytes, , )
STAT(ep_cursor_dropping_upper_threshold, , bytes, , )
STAT(ep_cursors_dropped, , count, , )
STAT(ep_cursor_memory_freed, , bytes, , )
STAT(ep_data_write_failed, , count, , )
STAT(ep_data_read_failed, , count, , )
STAT(ep_io_document_write_bytes, , bytes, , )
STAT(ep_io_total_read_bytes, , bytes, , )
STAT(ep_io_total_write_bytes, , bytes, , )
STAT(ep_io_compaction_read_bytes, , bytes, , )
STAT(ep_io_compaction_write_bytes, , bytes, , )
STAT(ep_io_bg_fetch_read_count, , count, , )
STAT(ep_bg_fetch_avg_read_amplification, , ratio, , )

// Magma stats
// Compaction.
STAT(ep_magma_compactions, , count, , )
STAT(ep_magma_flushes, , count, , )
STAT(ep_magma_ttl_compactions, , count, , )
STAT(ep_magma_filecount_compactions, , count, , )
STAT(ep_magma_writer_compactions, , count, , )
// Read amp.
STAT(ep_magma_readamp, , ratio, , )
STAT(ep_magma_readamp_get, , ratio, , )
STAT(ep_magma_read_bytes, , bytes, , )
STAT(ep_magma_read_bytes_compact, , bytes, , )
STAT(ep_magma_read_bytes_get, , bytes, , )
STAT(ep_magma_bytes_outgoing, , bytes, , )
// ReadIOAmp.
STAT(ep_magma_readio, , count, , )
STAT(ep_magma_readioamp, , ratio, , )
STAT(ep_magma_bytes_per_read, , ratio, , )
// Write amp.
STAT(ep_magma_writeamp, , ratio, , )
STAT(ep_magma_bytes_incoming, , bytes, , )
STAT(ep_magma_write_bytes, , bytes, , )
STAT(ep_magma_write_bytes_compact, , bytes, , )
// Fragmentation.
STAT(ep_magma_logical_data_size, , bytes, , )
STAT(ep_magma_logical_disk_size, , bytes, , )
STAT(ep_magma_fragmentation, , ratio, , )
// Disk usage.
STAT(ep_magma_total_disk_usage, , bytes, , )
STAT(ep_magma_wal_disk_usage, , bytes, , )
// Memory usage.
STAT(ep_magma_block_cache_mem_used, , bytes, , )
STAT(ep_magma_write_cache_mem_used, , bytes, , )
STAT(ep_magma_wal_mem_used, , bytes, , )
STAT(ep_magma_table_meta_mem_used, , bytes, , )
STAT(ep_magma_buffer_mem_used, , bytes, , )
STAT(ep_magma_bloom_filter_mem_used, , bytes, , )
STAT(ep_magma_index_resident_ratio, , ratio, , )
// Block cache.
STAT(ep_magma_block_cache_hits, , count, , )
STAT(ep_magma_block_cache_misses, , count, , )
STAT(ep_magma_block_cache_hit_ratio, , ratio, , )
// SST file count.
STAT(ep_magma_tables_created, , count, , )
STAT(ep_magma_tables_deleted, , count, , )
STAT(ep_magma_tables, , count, , )
// NSync.
STAT(ep_magma_syncs, , count, , )

STAT(ep_rocksdb_kMemTableTotal, , bytes, , )
STAT(ep_rocksdb_kMemTableUnFlushed, , bytes, , )
STAT(ep_rocksdb_kTableReadersTotal, , bytes, , )
STAT(ep_rocksdb_kCacheTotal, , bytes, , )
STAT(ep_rocksdb_default_kSizeAllMemTables, , bytes, , )
STAT(ep_rocksdb_seqno_kSizeAllMemTables, , bytes, , )
STAT(ep_rocksdb_block_cache_data_hit_ratio, , ratio, , )
STAT(ep_rocksdb_block_cache_index_hit_ratio, , ratio, , )
STAT(ep_rocksdb_block_cache_filter_hit_ratio, , ratio, , )
STAT(ep_rocksdb_default_kTotalSstFilesSize, , bytes, , )
STAT(ep_rocksdb_seqno_kTotalSstFilesSize, , bytes, , )
STAT(ep_rocksdb_scan_totalSeqnoHits, , count, , )
STAT(ep_rocksdb_scan_oldSeqnoHits, , count, , )

// EPBucket::getFileStats
STAT(ep_db_data_size, , bytes, , )
STAT(ep_db_file_size, , bytes, , )
STAT(ep_db_prepare_size, , bytes, , )

// Timing stats
STAT(bg_wait, , microseconds, , )
STAT(bg_load, , microseconds, , )
STAT(set_with_meta, , microseconds, , )
STAT(pending_ops, , microseconds, , )
STAT(access_scanner, , microseconds, , )
STAT(checkpoint_remover, , microseconds, , )
STAT(item_pager, , microseconds, , )
STAT(expiry_pager, , microseconds, , )
STAT(storage_age, , microseconds, , )
STAT(get_cmd, , microseconds, cmd_time_taken, LABEL(op, get))
STAT(store_cmd, , microseconds, cmd_time_taken, LABEL(op, store))
STAT(arith_cmd, , microseconds, cmd_time_taken, LABEL(op, arith))
STAT(get_stats_cmd, , microseconds, cmd_time_taken, LABEL(op, get_stats))
STAT(get_vb_cmd, , microseconds, cmd_time_taken, LABEL(op, get_vb))
STAT(set_vb_cmd, , microseconds, cmd_time_taken, LABEL(op, set_vb))
STAT(del_vb_cmd, , microseconds, cmd_time_taken, LABEL(op, del_vb))
STAT(chk_persistence_cmd,
     ,
     microseconds,
     cmd_time_taken,
     LABEL(op, chk_persistence))
STAT(notify_io, , microseconds, , )
STAT(disk_insert, , microseconds, disk, LABEL(op, insert))
STAT(disk_update, , microseconds, disk, LABEL(op, update))
STAT(disk_del, , microseconds, disk, LABEL(op, del))
STAT(disk_vb_del, , microseconds, disk, LABEL(op, vb_del))
STAT(disk_commit, , microseconds, disk, LABEL(op, commit))
STAT(item_alloc_sizes,
     ,
     bytes,
     , ) // TODO: this is not timing related but is in doTimingStats
STAT(bg_batch_size,
     ,
     count,
     , ) // TODO: this is not timing related but is in doTimingStats
STAT(persistence_cursor_get_all_items,
     ,
     microseconds,
     cursor_get_all_items_time,
     LABEL(cursor_type, persistence))
STAT(dcp_cursors_get_all_items,
     ,
     microseconds,
     cursor_get_all_items_time,
     LABEL(cursor_type, dcp))
STAT(sync_write_commit_majority,
     ,
     microseconds,
     sync_write_commit_duration,
     LABEL(level, majority))
STAT(sync_write_commit_majority_and_persist_on_master,
     ,
     microseconds,
     sync_write_commit_duration,
     LABEL(level, majority_and_persist_on_master))
STAT(sync_write_commit_persist_to_majority,
     ,
     microseconds,
     sync_write_commit_duration,
     LABEL(level, persist_to_majority))

// server_stats
STAT(uptime, , seconds, , )
STAT(stat_reset,
     ,
     none,
     , ) // TODO: String indicating when stats were reset. Change
         //  to a numeric stat for Prometheus?
STAT(time, , seconds, , )
STAT(version, , none, , ) // version string
STAT(memcached_version, , none, , ) // version string
STAT(daemon_connections, , count, , )
STAT(curr_connections, , count, , )
STAT(system_connections, , count, , )
STAT(total_connections, , count, , ) // total since start/reset
STAT(connection_structures, , count, , )
CBSTAT(cmd_get, , count) // this is not exposed to Prometheus as it duplicates
                         // the information provided by get_hits+get_misses
STAT(cmd_set, , count, ops, LABEL(op, set))
STAT(cmd_flush, , count, operations, LABEL(op, flush))
STAT(cmd_lock, , count, operations, LABEL(op, lock))
STAT(cmd_subdoc_lookup, , count, subdoc_operations, LABEL(op, lookup))
STAT(cmd_subdoc_mutation, , count, subdoc_operations, LABEL(op, mutation))
STAT(bytes_subdoc_lookup_total,
     ,
     bytes,
     subdoc_lookup_searched, ) // type _bytes will be suffixed
STAT(bytes_subdoc_lookup_extracted, , bytes, subdoc_lookup_extracted, )
STAT(bytes_subdoc_mutation_total, , bytes, subdoc_mutation_updated, )
STAT(bytes_subdoc_mutation_inserted, , bytes, subdoc_mutation_inserted, )
// aggregates over all buckets
STAT(cmd_total_sets, , count, , )
STAT(cmd_total_gets, , count, , )
STAT(cmd_total_ops, , count, , )
// aggregates over multiple operations for a single bucket
STAT(cmd_mutation, , count, , )
STAT(cmd_lookup, , count, , )

STAT(auth_cmds, , count, , )
STAT(auth_errors, , count, , )
STAT(get_hits, , count, ops, LABEL(op, get), LABEL(result, hit))
STAT(get_misses, , count, ops, LABEL(op, get), LABEL(result, miss))
STAT(delete_misses, , count, ops, LABEL(op, delete), LABEL(result, miss))
STAT(delete_hits, , count, ops, LABEL(op, delete), LABEL(result, hit))
STAT(incr_misses, , count, ops, LABEL(op, incr), LABEL(result, miss))
STAT(incr_hits, , count, ops, LABEL(op, incr), LABEL(result, hit))
STAT(decr_misses, , count, ops, LABEL(op, decr), LABEL(result, miss))
STAT(decr_hits, , count, ops, LABEL(op, decr), LABEL(result, hit))
STAT(cas_misses, , count, ops, LABEL(op, cas), LABEL(result, miss))
STAT(cas_hits, , count, ops, LABEL(op, cas), LABEL(result, hit))
STAT(cas_badval, , count, ops, LABEL(op, cas), LABEL(result, badval))
STAT(bytes_read, , bytes, read, ) // type _bytes will be suffixed
STAT(bytes_written, , bytes, written, )
STAT(rejected_conns, , count, , )
STAT(threads, , count, , )
STAT(conn_yields, , count, , )
STAT(iovused_high_watermark, , none, , )
STAT(msgused_high_watermark, , none, , )
STAT(lock_errors, , count, , )
STAT(cmd_lookup_10s_count, , count, , )
// us suffix would be confusing in Prometheus as the stat is scaled to seconds
STAT(cmd_lookup_10s_duration_us, , microseconds, cmd_lookup_10s_duration, )
STAT(cmd_mutation_10s_count, , count, , )
// us suffix would be confusing in Prometheus as the stat is scaled to seconds
STAT(cmd_mutation_10s_duration_us, , microseconds, cmd_mutation_10s_duration, )
STAT(total_resp_errors, , count, , )
STAT(audit_enabled, "enabled", none, audit_enabled, )
STAT(audit_dropped_events, "dropped_events", count, audit_dropped_events, )

// Vbucket aggreagated stats
#define VB_AGG_STAT(enumKey, unit, familyName, ...) \
    STAT(vb_active_##enumKey,                       \
         ,                                          \
         unit,                                      \
         familyName,                                \
         LABEL(state, active),                      \
         ##__VA_ARGS__)                             \
    STAT(vb_replica_##enumKey,                      \
         ,                                          \
         unit,                                      \
         familyName,                                \
         LABEL(state, replica),                     \
         ##__VA_ARGS__)                             \
    STAT(vb_pending_##enumKey,                      \
         ,                                          \
         unit,                                      \
         familyName,                                \
         LABEL(state, pending),                     \
         ##__VA_ARGS__)

VB_AGG_STAT(num, count, num_vbuckets, )
VB_AGG_STAT(curr_items, count, , )
VB_AGG_STAT(hp_vb_req_size, count, num_high_pri_requests, )
VB_AGG_STAT(num_non_resident, count, , )
VB_AGG_STAT(perc_mem_resident, percent, , )
VB_AGG_STAT(eject, count, , )
VB_AGG_STAT(expired, count, , )
VB_AGG_STAT(meta_data_memory, bytes, , )
VB_AGG_STAT(meta_data_disk, bytes, , )
VB_AGG_STAT(checkpoint_memory, bytes, , )
VB_AGG_STAT(checkpoint_memory_unreferenced, bytes, , )
VB_AGG_STAT(checkpoint_memory_overhead, bytes, , )
VB_AGG_STAT(ht_memory, bytes, , )
VB_AGG_STAT(itm_memory, bytes, , )
VB_AGG_STAT(itm_memory_uncompressed, bytes, , )
VB_AGG_STAT(ops_create, count, , )
VB_AGG_STAT(ops_update, count, , )
VB_AGG_STAT(ops_delete, count, , )
VB_AGG_STAT(ops_get, count, , )
VB_AGG_STAT(ops_reject, count, , )
VB_AGG_STAT(queue_size, count, , )
VB_AGG_STAT(queue_memory, bytes, , )
VB_AGG_STAT(queue_age, milliseconds, , )
VB_AGG_STAT(queue_pending, bytes, , )
VB_AGG_STAT(queue_fill, count, , )
VB_AGG_STAT(queue_drain, count, , )
VB_AGG_STAT(rollback_item_count, count, , )

#undef VB_AGG_STAT

STAT(curr_items, , count, , )
STAT(curr_temp_items, , count, , )
STAT(curr_items_tot, , count, , )

STAT(vb_active_sync_write_accepted_count, , count, , )
STAT(vb_active_sync_write_committed_count, , count, , )
STAT(vb_active_sync_write_aborted_count, , count, , )
STAT(vb_replica_sync_write_accepted_count, , count, , )
STAT(vb_replica_sync_write_committed_count, , count, , )
STAT(vb_replica_sync_write_aborted_count, , count, , )
STAT(vb_dead_num, , count, , )
STAT(ep_vb_total, , count, , )
STAT(ep_total_new_items, , count, , )
STAT(ep_total_del_items, , count, , )
STAT(ep_diskqueue_memory, , bytes, , )
STAT(ep_diskqueue_fill, , count, , )
STAT(ep_diskqueue_drain, , count, , )
STAT(ep_diskqueue_pending, , count, , )
STAT(ep_meta_data_memory, , bytes, , )
STAT(ep_meta_data_disk, , bytes, , )
STAT(ep_checkpoint_memory, , bytes, , )
STAT(ep_checkpoint_memory_unreferenced, , bytes, , )
STAT(ep_checkpoint_memory_overhead, , bytes, , )
STAT(ep_total_cache_size, , bytes, , )
STAT(rollback_item_count, , count, , )
STAT(ep_num_non_resident, , count, , )
STAT(ep_chk_persistence_remains, , count, , )
STAT(ep_active_hlc_drift, , microseconds, , )
STAT(ep_active_hlc_drift_count, , count, , )
STAT(ep_replica_hlc_drift, , microseconds, , )
STAT(ep_replica_hlc_drift_count, , count, , )
STAT(ep_active_ahead_exceptions, , count, , )
STAT(ep_active_behind_exceptions, , count, , )
STAT(ep_replica_ahead_exceptions, , count, , )
STAT(ep_replica_behind_exceptions, , count, , )
STAT(ep_clock_cas_drift_threshold_exceeded, , count, , )

STAT(connagg_connection_count,
     "{connection_type}:count",
     count,
     dcp_connection_count, )
STAT(connagg_backoff, "{connection_type}:backoff", count, dcp_backoff, )
STAT(connagg_producer_count,
     "{connection_type}:producer_count",
     count,
     dcp_producer_count, )
STAT(connagg_items_sent,
     "{connection_type}:items_sent",
     count,
     dcp_items_sent, )
STAT(connagg_items_remaining,
     "{connection_type}:items_remaining",
     count,
     dcp_items_remaining, )
STAT(connagg_total_bytes,
     "{connection_type}:total_bytes",
     bytes,
     dcp_total_data_size, )
STAT(connagg_total_uncompressed_data_size,
     "{connection_type}:total_uncompressed_data_size",
     bytes,
     dcp_total_uncompressed_data_size, )

STAT(manifest_uid, , none, , )
STAT(manifest_force, "force", none, , )

STAT(collection_name, "name", none, , )
STAT(collection_scope_name, "scope_name", none, , )
STAT(collection_maxTTL, "maxTTL", seconds, , )

STAT(scope_name, "name", none, , )
STAT(scope_collection_count, "collections", count, , )

STAT(collection_mem_used, "collections_mem_used", bytes, , )
STAT(collection_item_count, "items", count, , )
STAT(collection_disk_size, "disk_size", bytes, , )

STAT(collection_ops_store, "ops_store", count, collection_ops, LABEL(op, store))
STAT(collection_ops_delete,
     "ops_delete",
     count,
     collection_ops,
     LABEL(op, delete))
STAT(collection_ops_get, "ops_get", count, collection_ops, LABEL(op, get))
