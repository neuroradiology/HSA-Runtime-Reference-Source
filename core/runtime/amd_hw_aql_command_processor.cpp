////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2014 ADVANCED MICRO DEVICES, INC.
//
// AMD is granting you permission to use this software and documentation(if any)
// (collectively, the “Materials”) pursuant to the terms and conditions of the
// Software License Agreement included with the Materials.If you do not have a
// copy of the Software License Agreement, contact your AMD representative for a
// copy.
//
// You agree that you will not reverse engineer or decompile the Materials, in
// whole or in part, except as allowed by applicable law.
//
// WARRANTY DISCLAIMER : THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND.AMD DISCLAIMS ALL WARRANTIES, EXPRESS, IMPLIED, OR STATUTORY,
// INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE, NON - INFRINGEMENT, THAT THE
// SOFTWARE WILL RUN UNINTERRUPTED OR ERROR - FREE OR WARRANTIES ARISING FROM
// CUSTOM OF TRADE OR COURSE OF USAGE.THE ENTIRE RISK ASSOCIATED WITH THE USE OF
// THE SOFTWARE IS ASSUMED BY YOU.Some jurisdictions do not allow the exclusion
// of implied warranties, so the above exclusion may not apply to You.
//
// LIMITATION OF LIABILITY AND INDEMNIFICATION : AMD AND ITS LICENSORS WILL NOT,
// UNDER ANY CIRCUMSTANCES BE LIABLE TO YOU FOR ANY PUNITIVE, DIRECT,
// INCIDENTAL, INDIRECT, SPECIAL OR CONSEQUENTIAL DAMAGES ARISING FROM USE OF
// THE SOFTWARE OR THIS AGREEMENT EVEN IF AMD AND ITS LICENSORS HAVE BEEN
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.In no event shall AMD's total
// liability to You for all damages, losses, and causes of action (whether in
// contract, tort (including negligence) or otherwise) exceed the amount of $100
// USD.  You agree to defend, indemnify and hold harmless AMD and its licensors,
// and any of their directors, officers, employees, affiliates or agents from
// and against any and all loss, damage, liability and other expenses (including
// reasonable attorneys' fees), resulting from Your use of the Software or
// violation of the terms and conditions of this Agreement.
//
// U.S.GOVERNMENT RESTRICTED RIGHTS : The Materials are provided with
// "RESTRICTED RIGHTS." Use, duplication, or disclosure by the Government is
// subject to the restrictions as set forth in FAR 52.227 - 14 and DFAR252.227 -
// 7013, et seq., or its successor.Use of the Materials by the Government
// constitutes acknowledgement of AMD's proprietary rights in them.
//
// EXPORT RESTRICTIONS: The Materials may be subject to export restrictions as
//                      stated in the Software License Agreement.
//
////////////////////////////////////////////////////////////////////////////////

#include "core/inc/amd_hw_aql_command_processor.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#endif

#include <stdio.h>
#include <string.h>

#include "core/inc/runtime.h"
#include "core/inc/amd_memory_region.h"
#include "core/inc/signal.h"
#include "core/inc/queue.h"
#include "core/util/utils.h"

// When set to 1, the ring buffer is internally doubled in size.
// Virtual addresses in the upper half of the ring allocation
// are mapped to the same set of pages backing the lower half.
// Values written to the HW doorbell are modulo the doubled size.
// This allows the HW to accept (doorbell == last_doorbell + queue_size).
// This workaround is required for Gfx7 and Gfx8 ASICs.
#define QUEUE_FULL_WORKAROUND 1

namespace amd {
uint32_t ComputeRingBufferMinPkts() {
  // From CP_HQD_PQ_CONTROL.QUEUE_SIZE specification:
  //   Size of the primary queue (PQ) will be: 2^(HQD_QUEUE_SIZE+1) DWs.
  //   Min Size is 7 (2^8 = 256 DWs) and max size is 29 (2^30 = 1 G-DW)
  uint32_t min_bytes = 0x400;

#if QUEUE_FULL_WORKAROUND
#ifdef __linux__
  // Double mapping requires one page of backing store.
  min_bytes = Max(min_bytes, 0x1000U);
#endif
#ifdef _WIN32
  // Shared memory mapping is at system allocation granularity.
  SYSTEM_INFO sys_info;
  GetNativeSystemInfo(&sys_info);
  min_bytes = Max(min_bytes, uint32_t(sys_info.dwAllocationGranularity));
#endif
#endif

  return uint32_t(min_bytes / sizeof(core::AqlPacket));
}

// From CP_HQD_PQ_BASE specification:
//   256 Byte aligned Queue Base Address. Bits[31:0] = BaseAddr[39:8].
const uint32_t kRingBufferAlignBytes = 0x100;

// Restrict queues to 8MB to guard against programming errors.
const uint32_t kRingBufferMaxPkts = 0x20000;

// Minimum queue size is a function of several restrictions.
const uint32_t kRingBufferMinPkts = ComputeRingBufferMinPkts();

// Queue::amd_queue_ is cache-aligned for performance.
const uint32_t kAmdQueueAlignBytes = 0x40;

void* HwAqlCommandProcessor::operator new(size_t size) {
  // Align base to 64B to enforce amd_queue_ member alignment.
  return _aligned_malloc(size, kAmdQueueAlignBytes);
}

void HwAqlCommandProcessor::operator delete(void* ptr) { _aligned_free(ptr); }

HwAqlCommandProcessor::HwAqlCommandProcessor(GpuAgent* agent,
                                             size_t req_size_pkts,
                                             HSAuint32 node_id,
                                             ScratchInfo& scratch)
    : Signal(0),
      ring_buf_(NULL),
      ring_buf_alloc_bytes_(0),
      queue_id_(HSA_QUEUEID(-1)),
      valid_(false),
      agent_(agent),
      queue_scratch_(scratch) {
  do {
    // Register the amd_queue_ field for CP access.
    hsa_status_t hsa_status;
    hsa_status = hsa_memory_register(&amd_queue_, sizeof(amd_queue_));
    if (hsa_status != HSA_STATUS_SUCCESS) break;

    // Apply sizing constraints to the ring buffer.
    uint32_t queue_size_pkts = uint32_t(req_size_pkts);
    queue_size_pkts = Min(queue_size_pkts, kRingBufferMaxPkts);
    queue_size_pkts = Max(queue_size_pkts, kRingBufferMinPkts);

    uint32_t queue_size_bytes = queue_size_pkts * sizeof(core::AqlPacket);
    if ((queue_size_bytes & (queue_size_bytes - 1)) != 0) break;

    // Allocate the AQL packet ring buffer.
    AllocRegisteredRingBuffer(queue_size_pkts);
    if (ring_buf_ == NULL) break;

    // Fill the ring buffer with ALWAYS_RESERVED packet headers.
    // Leave packet content uninitialized to help track errors.
    for (uint32_t pkt_id = 0; pkt_id < queue_size_pkts; ++pkt_id) {
      ((uint32_t*)ring_buf_)[16 * pkt_id] = HSA_PACKET_TYPE_ALWAYS_RESERVED;
    }

    // Zero the amd_queue_ structure to clear RPTR/WPTR before queue attach.
    memset(&amd_queue_, 0, sizeof(amd_queue_));

    // Initialize and map a CP AQL queue.
    HsaQueueResource queue_rsrc = {0};
    queue_rsrc.Queue_read_ptr_aql = (uint64_t*)&amd_queue_.read_dispatch_id;
    queue_rsrc.Queue_write_ptr_aql = (uint64_t*)&amd_queue_.write_dispatch_id;

    HSAKMT_STATUS kmt_status;
    kmt_status = hsaKmtCreateQueue(node_id, HSA_QUEUE_COMPUTE_AQL, 100,
                                   HSA_QUEUE_PRIORITY_NORMAL, ring_buf_,
                                   ring_buf_alloc_bytes_, NULL, &queue_rsrc);
    if (kmt_status != HSAKMT_STATUS_SUCCESS) break;
    queue_id_ = queue_rsrc.QueueId;

    // Populate doorbell signal structure.
    memset(&signal_, 0, sizeof(signal_));
    signal_.type = core::kHsaSignalAmdKvDoorbell;
    signal_.doorbell_ptr = (volatile uint32_t*)queue_rsrc.Queue_DoorBell;

    // Populate amd_queue_ structure.
    amd_queue_.hsa_queue.queue_type = HSA_QUEUE_TYPE_SINGLE;
    amd_queue_.hsa_queue.queue_features = HSA_QUEUE_FEATURE_DISPATCH;
    amd_queue_.hsa_queue.base_address = uint64_t(uintptr_t(ring_buf_));
    amd_queue_.hsa_queue.doorbell_signal = Signal::Convert(this);
    amd_queue_.hsa_queue.size = queue_size_pkts;
    amd_queue_.hsa_queue.id = core::Runtime::runtime_singleton_->GetQueueId();
    amd_queue_.read_dispatch_id_field_base_byte_offset = uint32_t(
        uintptr_t(&amd_queue_.read_dispatch_id) - uintptr_t(&amd_queue_));

#ifdef HSA_LARGE_MODEL
    amd_queue_.is_ptr64 = 1;
#else
    amd_queue_.is_ptr64 = 0;
#endif

    // Populate scratch resource descriptor in amd_queue_.

  #include "core/inc/registers.h"    
    SQ_BUF_RSRC_WORD0 srd0;
    SQ_BUF_RSRC_WORD1 srd1;
    SQ_BUF_RSRC_WORD2 srd2;
    SQ_BUF_RSRC_WORD3 srd3;
    uintptr_t scratch_base = uintptr_t(queue_scratch_.queue_base);
    uint32_t scratch_base_hi = 0;

#ifdef HSA_LARGE_MODEL
    scratch_base_hi = uint32_t(scratch_base >> 32);
#endif

    srd0.bits.BASE_ADDRESS = uint32_t(scratch_base);
    srd1.bits.BASE_ADDRESS_HI = scratch_base_hi;
    srd1.bits.STRIDE = 0;
    srd1.bits.CACHE_SWIZZLE = 0;
    srd1.bits.SWIZZLE_ENABLE = 1;
    srd2.bits.NUM_RECORDS = uint32_t(queue_scratch_.size);
    srd3.bits.DST_SEL_X = SQ_SEL_X;
    srd3.bits.DST_SEL_Y = SQ_SEL_Y;
    srd3.bits.DST_SEL_Z = SQ_SEL_Z;
    srd3.bits.DST_SEL_W = SQ_SEL_W;
    srd3.bits.NUM_FORMAT = BUF_NUM_FORMAT_UINT;
    srd3.bits.DATA_FORMAT = BUF_DATA_FORMAT_32;
    srd3.bits.ELEMENT_SIZE = 1;  // 4
    srd3.bits.INDEX_STRIDE = 3;  // 64
    srd3.bits.ADD_TID_ENABLE = 1;
    srd3.bits.ATC__CI__VI = 1;
    srd3.bits.HASH_ENABLE = 0;
    srd3.bits.HEAP = 0;
    srd3.bits.MTYPE__CI__VI = 0;
    srd3.bits.TYPE = SQ_RSRC_BUF;

    amd_queue_.scratch_resource_descriptor[0] = srd0.u32All;
    amd_queue_.scratch_resource_descriptor[1] = srd1.u32All;
    amd_queue_.scratch_resource_descriptor[2] = srd2.u32All;
    amd_queue_.scratch_resource_descriptor[3] = srd3.u32All;
  
// Populate flat scratch parameters in amd_queue_.
#if 0
      // Per-VMID scratch pool with queue offsets not yet configured.
      amd_queue_.scratch_backing_memory_location = 0;
#endif
    amd_queue_.scratch_backing_memory_byte_size = queue_scratch_.size;
    amd_queue_.scratch_workitem_byte_size =
        uint32_t(queue_scratch_.size_per_thread);

    // Set concurrent wavefront limits when scratch is being used.
    COMPUTE_TMPRING_SIZE tmpring_size = {0};

    tmpring_size.bits.WAVES =
        (queue_scratch_.size / queue_scratch_.size_per_thread / 64);
    tmpring_size.bits.WAVESIZE =
        (((64 * queue_scratch_.size_per_thread) + 1023) / 1024);

    amd_queue_.compute_tmpring_size = tmpring_size.u32All;

    // Set group and private memory apertures in amd_queue_.
    auto& regions = agent->regions();

    for (int i = 0; i < regions.size(); i++) {
      const MemoryRegion* amdregion;
      amdregion = static_cast<const MemoryRegion*>(regions[i]);
      void* base;
      amdregion->GetInfo(HSA_REGION_INFO_BASE, &base);

      if (amdregion->IsLDS()) {
#ifdef HSA_LARGE_MODEL
        amd_queue_.group_segment_aperture_base_hi =
            uint32_t(uintptr_t(base) >> 32);
#else
        amd_queue_.group_segment_aperture_base_hi = uint32_t(base);
#endif
      }

      if (amdregion->IsScratch()) {
#ifdef HSA_LARGE_MODEL
        amd_queue_.private_segment_aperture_base_hi =
            uint32_t(uintptr_t(base) >> 32);
#else
        amd_queue_.private_segment_aperture_base_hi = uint32_t(base);
#endif
      }
    }

    assert(amd_queue_.group_segment_aperture_base_hi != NULL &&
           "No group region found.");

#if 0
      // Private aperture is not yet provided by thunk.
      assert(amd_queue_.private_segment_aperture_base_hi != NULL &&
        "No private region found.");
#endif

    valid_ = true;
    return;
  } while (false);

  if (queue_id_ != HSA_QUEUEID(-1)) hsaKmtDestroyQueue(queue_id_);
  FreeRegisteredRingBuffer();
  hsa_memory_deregister(&amd_queue_, sizeof(amd_queue_));
}

HwAqlCommandProcessor::~HwAqlCommandProcessor() {
  if (!IsValid()) {
    return;
  }

  hsaKmtDestroyQueue(queue_id_);
  FreeRegisteredRingBuffer();
  hsa_memory_deregister(&amd_queue_, sizeof(amd_queue_));
  agent_->ReleaseQueueScratch(queue_scratch_.queue_base);
}

uint64_t HwAqlCommandProcessor::LoadReadIndexAcquire() {
  return DispatchIdToNumPackets(
      atomic::Load(&amd_queue_.read_dispatch_id, std::memory_order_acquire));
}

uint64_t HwAqlCommandProcessor::LoadReadIndexRelaxed() {
  return DispatchIdToNumPackets(
      atomic::Load(&amd_queue_.read_dispatch_id, std::memory_order_relaxed));
}

uint64_t HwAqlCommandProcessor::LoadWriteIndexAcquire() {
  return DispatchIdToNumPackets(
      atomic::Load(&amd_queue_.write_dispatch_id, std::memory_order_acquire));
}

uint64_t HwAqlCommandProcessor::LoadWriteIndexRelaxed() {
  return DispatchIdToNumPackets(
      atomic::Load(&amd_queue_.write_dispatch_id, std::memory_order_relaxed));
}

void HwAqlCommandProcessor::StoreWriteIndexRelaxed(uint64_t value) {
  atomic::Store(&amd_queue_.write_dispatch_id, NumPacketsToDispatchId(value),
                std::memory_order_relaxed);
}

void HwAqlCommandProcessor::StoreWriteIndexRelease(uint64_t value) {
  atomic::Store(&amd_queue_.write_dispatch_id, NumPacketsToDispatchId(value),
                std::memory_order_release);
}

uint64_t HwAqlCommandProcessor::CasWriteIndexAcqRel(uint64_t expected,
                                                    uint64_t value) {
  return DispatchIdToNumPackets(
      atomic::Cas(&amd_queue_.write_dispatch_id, NumPacketsToDispatchId(value),
                  NumPacketsToDispatchId(expected), std::memory_order_acq_rel));
}
uint64_t HwAqlCommandProcessor::CasWriteIndexAcquire(uint64_t expected,
                                                     uint64_t value) {
  return DispatchIdToNumPackets(
      atomic::Cas(&amd_queue_.write_dispatch_id, NumPacketsToDispatchId(value),
                  NumPacketsToDispatchId(expected), std::memory_order_acquire));
}
uint64_t HwAqlCommandProcessor::CasWriteIndexRelaxed(uint64_t expected,
                                                     uint64_t value) {
  return DispatchIdToNumPackets(
      atomic::Cas(&amd_queue_.write_dispatch_id, NumPacketsToDispatchId(value),
                  NumPacketsToDispatchId(expected), std::memory_order_relaxed));
}
uint64_t HwAqlCommandProcessor::CasWriteIndexRelease(uint64_t expected,
                                                     uint64_t value) {
  return DispatchIdToNumPackets(
      atomic::Cas(&amd_queue_.write_dispatch_id, NumPacketsToDispatchId(value),
                  NumPacketsToDispatchId(expected), std::memory_order_release));
}

uint64_t HwAqlCommandProcessor::AddWriteIndexAcqRel(uint64_t value) {
  return DispatchIdToNumPackets(atomic::Add(&amd_queue_.write_dispatch_id,
                                            NumPacketsToDispatchId(value),
                                            std::memory_order_acq_rel));
}

uint64_t HwAqlCommandProcessor::AddWriteIndexAcquire(uint64_t value) {
  return DispatchIdToNumPackets(atomic::Add(&amd_queue_.write_dispatch_id,
                                            NumPacketsToDispatchId(value),
                                            std::memory_order_acquire));
}

uint64_t HwAqlCommandProcessor::AddWriteIndexRelaxed(uint64_t value) {
  return DispatchIdToNumPackets(atomic::Add(&amd_queue_.write_dispatch_id,
                                            NumPacketsToDispatchId(value),
                                            std::memory_order_relaxed));
}

uint64_t HwAqlCommandProcessor::AddWriteIndexRelease(uint64_t value) {
  return DispatchIdToNumPackets(atomic::Add(&amd_queue_.write_dispatch_id,
                                            NumPacketsToDispatchId(value),
                                            std::memory_order_release));
}

void HwAqlCommandProcessor::StoreRelaxed(hsa_signal_value_t value) {
  // Gfx7/Gfx8 microcode expects doorbell value beyond packet,
  // not ahead of it, and to wrap at the end of the ring.
  uint32_t hw_ring_size_pkts =
      uint32_t(ring_buf_alloc_bytes_ / sizeof(core::AqlPacket));
  uint32_t hw_value = uint32_t(NumPacketsToDispatchId((value + 1) % hw_ring_size_pkts));
  *signal_.doorbell_ptr = hw_value;
}

void HwAqlCommandProcessor::StoreRelease(hsa_signal_value_t value) {
  std::atomic_thread_fence(std::memory_order_release);
  StoreRelaxed(value);
}

void HwAqlCommandProcessor::AllocRegisteredRingBuffer(
    uint32_t queue_size_pkts) {
#if QUEUE_FULL_WORKAROUND
  // Compute the physical and virtual size of the queue.
  uint32_t ring_buf_phys_size_bytes =
      uint32_t(queue_size_pkts * sizeof(core::AqlPacket));
  ring_buf_alloc_bytes_ = 2 * ring_buf_phys_size_bytes;

#ifdef __linux__
  // Create a system-unique shared memory path for this thread.
  char ring_buf_shm_path[16];
  pid_t sys_unique_tid = pid_t(syscall(__NR_gettid));
  sprintf(ring_buf_shm_path, "/%u", sys_unique_tid);

  int ring_buf_shm_fd = -1;
  void* reserve_va = NULL;

  do {
    // Create a shared memory object to back the ring buffer.
    ring_buf_shm_fd = shm_open(ring_buf_shm_path, O_CREAT | O_RDWR | O_EXCL,
                               S_IRUSR | S_IWUSR);
    if (ring_buf_shm_fd == -1) {
      break;
    }
    if (ftruncate(ring_buf_shm_fd, ring_buf_phys_size_bytes) == -1) {
      break;
    }

    // Reserve a VA range twice the size of the physical backing store.
    reserve_va = mmap(NULL, ring_buf_alloc_bytes_, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // Remap the lower and upper halves of the VA range.
    // Map both halves to the shared memory backing store.
    void* ring_buf_lower_half =
        mmap(reserve_va, ring_buf_phys_size_bytes, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_FIXED, ring_buf_shm_fd, 0);

    void* ring_buf_upper_half =
        mmap((void*)(uintptr_t(reserve_va) + ring_buf_phys_size_bytes),
             ring_buf_phys_size_bytes, PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_FIXED, ring_buf_shm_fd, 0);

    // Release explicit reference to shared memory object.
    shm_unlink(ring_buf_shm_path);
    close(ring_buf_shm_fd);

    // Successfully created mapping.
    ring_buf_ = ring_buf_lower_half;
    return;
  } while (false);

  // Resource cleanup on failure.
  if (reserve_va) munmap(reserve_va, ring_buf_alloc_bytes_);
  if (ring_buf_shm_fd != -1) {
    shm_unlink(ring_buf_shm_path);
    close(ring_buf_shm_fd);
  }
#endif
#ifdef _WIN32
  HANDLE ring_buf_mapping = INVALID_HANDLE_VALUE;
  void* ring_buf_lower_half = NULL;
  void* ring_buf_upper_half = NULL;

  do {
    // Create a page file mapping to back the ring buffer.
    ring_buf_mapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL,
                                         PAGE_READWRITE | SEC_COMMIT, 0,
                                         ring_buf_phys_size_bytes, NULL);
    if (ring_buf_mapping == NULL) {
      break;
    }

    // Retry until obtaining an appropriate virtual address mapping.
    for (int num_attempts = 0; num_attempts < 1000; ++num_attempts) {
      // Find a virtual address range twice the size of the file mapping.
      void* reserve_va =
          VirtualAllocEx(GetCurrentProcess(), NULL, ring_buf_alloc_bytes_,
                         MEM_TOP_DOWN | MEM_RESERVE, PAGE_READWRITE);
      if (reserve_va == NULL) {
        break;
      }
      VirtualFree(reserve_va, 0, MEM_RELEASE);

      // Map the ring buffer into the free virtual range.
      // This may fail: another thread can allocate in this range.
      ring_buf_lower_half =
          MapViewOfFileEx(ring_buf_mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                          ring_buf_phys_size_bytes, reserve_va);

      if (ring_buf_lower_half == NULL) {
        // Virtual range allocated by another thread, try again.
        continue;
      }

      ring_buf_upper_half = MapViewOfFileEx(
          ring_buf_mapping, FILE_MAP_ALL_ACCESS, 0, 0, ring_buf_phys_size_bytes,
          (void*)(uintptr_t(reserve_va) + ring_buf_phys_size_bytes));

      if (ring_buf_upper_half == NULL) {
        // Virtual range allocated by another thread, try again.
        UnmapViewOfFile(ring_buf_lower_half);
        continue;
      }

      // Successfully created mapping.
      ring_buf_ = ring_buf_lower_half;
      break;
    }

    if (ring_buf_ == NULL) {
      break;
    }

    // Release file mapping (reference counted by views).
    CloseHandle(ring_buf_mapping);

    // Don't register the memory: causes a failure in the KFD.
    // Instead use implicit registration to access the ring buffer.
    return;
  } while (false);

  // Resource cleanup on failure.
  UnmapViewOfFile(ring_buf_upper_half);
  UnmapViewOfFile(ring_buf_lower_half);
  CloseHandle(ring_buf_mapping);
#endif
#else
  // Allocate storage for the ring buffer.
  ring_buf_alloc_bytes_ = queue_size_pkts * sizeof(AqlPacket);
  ring_buf_ = _aligned_malloc(ring_buf_alloc_bytes_, kRingBufferAlignBytes);

  // Register the ring buffer for CP access.
  hsa_memory_register(ring_buf_, ring_buf_alloc_bytes_);
#endif
}

void HwAqlCommandProcessor::FreeRegisteredRingBuffer() {
  hsa_memory_deregister(ring_buf_, ring_buf_alloc_bytes_);

#if QUEUE_FULL_WORKAROUND
#ifdef __linux__
  munmap(ring_buf_, ring_buf_alloc_bytes_);
#endif
#ifdef _WIN32
  UnmapViewOfFile(ring_buf_);
  UnmapViewOfFile((void*)(uintptr_t(ring_buf_) + (ring_buf_alloc_bytes_ / 2)));
#endif
#else
  _aligned_free(ring_buf_);
#endif

  ring_buf_ = NULL;
  ring_buf_alloc_bytes_ = 0;
}

uint64_t HwAqlCommandProcessor::DispatchIdToNumPackets(uint64_t dispatch_id) {
  // Gfx7/Gfx8 microcode interprets amd_queue_t.read_dispatch_id and
  // .write_dispatch_id as counts in DWORDs, not AQL packets.
  return dispatch_id / (sizeof(core::AqlPacket) / sizeof(uint32_t));
}

uint64_t HwAqlCommandProcessor::NumPacketsToDispatchId(uint64_t num_pkts) {
  return num_pkts * (sizeof(core::AqlPacket) / sizeof(uint32_t));
}

}  // namespace amd
