////////////////////////////////////////////////////////////////////////////////
//
// Copyright 2014 ADVANCED MICRO DEVICES, INC.
//
// AMD is granting you permission to use this software and documentation(if any)
// (collectively, the "Materials") pursuant to the terms and conditions of the
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

// HSA AMD extension.

#ifndef HSA_RUNTIME_EXT_AMD_H_
#define HSA_RUNTIME_EXT_AMD_H_

#include "hsa.h"
#include "hsa_ext_finalize.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum hsa_amd_status_s {
  HSA_EXT_STATUS_INFO_HALT_ITERATION = HSA_STATUS_INFO_BREAK,
} hsa_amd_status_t;

typedef enum hsa_amd_agent_info_s {
  HSA_EXT_AGENT_INFO_DEVICE_ID = HSA_AGENT_INFO_COUNT,
  HSA_EXT_AGENT_INFO_CACHELINE_SIZE,
  HSA_EXT_AGENT_INFO_COMPUTE_UNIT_COUNT,
  HSA_EXT_AGENT_INFO_MAX_CLOCK_FREQUENCY,
  HSA_EXT_AGENT_INFO_DRIVER_NODE_ID
} hsa_amd_agent_info_t;

typedef enum hsa_amd_region_info_s {
  HSA_EXT_REGION_INFO_HOST_ACCESS = HSA_REGION_INFO_COUNT
} hsa_amd_region_info_t;

typedef enum hsa_amd_memory_type_s {
  HSA_EXT_MEMORY_TYPE_COHERENT = 0,
  HSA_EXT_MEMORY_TYPE_NONCOHERENT = 1
} hsa_amd_memory_type_t;

hsa_status_t HSA_API
    hsa_ext_get_memory_type(hsa_agent_t agent, hsa_amd_memory_type_t* type);

hsa_status_t HSA_API
    hsa_ext_set_memory_type(hsa_agent_t agent, hsa_amd_memory_type_t type);

typedef struct hsa_amd_dispatch_time_s {
  uint64_t start;
  uint64_t end;
} hsa_amd_dispatch_time_t;

hsa_status_t HSA_API hsa_ext_set_profiling(hsa_queue_t* queue, int enable);

hsa_status_t HSA_API hsa_ext_get_dispatch_times(hsa_agent_t agent,
                                                hsa_signal_t signal,
                                                hsa_amd_dispatch_time_t* time);


//===----------------------------------------------------------------------===//
// Extra Finalizer Core APIs.                                                 //
//===----------------------------------------------------------------------===//

hsa_status_t HSA_API
    hsa_ext_extra_query_call_convention_count(hsa_agent_t agent,
                                              uint32_t* call_convention_count);

//===----------------------------------------------------------------------===//
// Extra Linker Service Layer APIs.                                           //
//===----------------------------------------------------------------------===//

hsa_status_t HSA_API hsa_ext_extra_query_symbol_definition(
    hsa_ext_program_handle_t program, const char* symbol_name,
    hsa_ext_brig_module_handle_t* definition_module,
    hsa_ext_brig_module_t** definition_module_brig,
    hsa_ext_brig_code_section_offset32_t* definition_symbol);

hsa_status_t HSA_API
    hsa_ext_extra_query_program(hsa_ext_code_handle_t code,
                                hsa_ext_program_handle_t* program);


//===----------------------------------------------------------------------===//
// AMD Kernel Code.                                                           //
//===----------------------------------------------------------------------===//

typedef uint32_t hsa_amd_code_version32_t;

enum hsa_amd_code_version_t {
  HSA_EXT_AMD_CODE_VERSION_MAJOR = 0,
  HSA_EXT_AMD_CODE_VERSION_MINOR = 1
};

enum hsa_amd_system_vgpr_workitem_id_t {
  HSA_EXT_AMD_SYSTEM_VGPR_WORKITEM_ID_X = 0,
  HSA_EXT_AMD_SYSTEM_VGPR_WORKITEM_ID_X_Y = 1,
  HSA_EXT_AMD_SYSTEM_VGPR_WORKITEM_ID_X_Y_Z = 2,
  HSA_EXT_AMD_SYSTEM_VGPR_WORKITEM_ID_UNDEFINED = 3
};

enum hsa_amd_element_byte_size_t {
  HSA_EXT_AMD_ELEMENT_2_BYTES = 0,
  HSA_EXT_AMD_ELEMENT_4_BYTES = 1,
  HSA_EXT_AMD_ELEMENT_8_BYTES = 2,
  HSA_EXT_AMD_ELEMENT_16_BYTES = 3
};

enum hsa_amd_float_round_mode_t {
  HSA_EXT_AMD_FLOAT_ROUND_TO_NEAREST_EVEN = 0,
  HSA_EXT_AMD_FLOAT_ROUND_TO_PLUS_INFINITY = 1,
  HSA_EXT_AMD_FLOAT_ROUND_TO_MINUS_INFINITY = 2,
  HSA_EXT_AMD_FLOAT_ROUND_TO_ZERO = 3
};

enum hsa_amd_float_denorm_mode_t {
  HSA_EXT_AMD_FLOAT_DENORM_FLUSH_SOURCE_OUTPUT = 0,
  HSA_EXT_AMD_FLOAT_DENORM_FLUSH_OUTPUT = 1,
  HSA_EXT_AMD_FLOAT_DENORM_FLUSH_SOURCE = 2,
  HSA_EXT_AMD_FLOAT_DENORM_NO_FLUSH = 3
};

typedef struct hsa_amd_kernel_code_s {
  hsa_amd_code_version32_t amd_code_version_major;
  hsa_amd_code_version32_t amd_code_version_minor;
  uint32_t struct_byte_size;
  uint32_t target_chip;
  int64_t kernel_code_entry_byte_offset;
  int64_t kernel_code_prefetch_byte_offset;
  uint64_t kernel_code_prefetch_byte_size;
  uint64_t max_scratch_backing_memory_byte_size;
  uint32_t compute_pgm_rsrc1;
  uint32_t compute_pgm_rsrc2;
  uint32_t code_properties;
  uint32_t workitem_private_segment_byte_size;
  uint32_t workgroup_group_segment_byte_size;
  uint32_t gds_segment_byte_size;
  uint64_t kernarg_segment_byte_size;
  uint32_t workgroup_fbarrier_count;
  uint16_t wavefront_sgpr_count;
  uint16_t workitem_vgpr_count;
  uint16_t reserved_vgpr_first;
  uint16_t reserved_vgpr_count;
  uint16_t reserved_sgpr_first;
  uint16_t reserved_sgpr_count;
  uint16_t debug_wavefront_private_segment_offset_sgpr;
  uint16_t debug_private_segment_buffer_sgpr;
  hsa_powertwo8_t kernarg_segment_alignment;
  hsa_powertwo8_t group_segment_alignment;
  hsa_powertwo8_t private_segment_alignment;
  uint8_t reserved3;
  hsa_ext_code_kind32_t code_type;
  uint32_t reserved4;
  hsa_powertwo8_t wavefront_size;
  uint8_t optimization_level;
  hsa_ext_brig_profile8_t hsail_profile;
  hsa_ext_brig_machine_model8_t hsail_machine_model;
  uint32_t hsail_version_major;
  uint32_t hsail_version_minor;
  uint16_t reserved5;
  uint16_t reserved6;
  hsa_ext_control_directives_t control_directive;
} hsa_amd_kernel_code_t;

//===----------------------------------------------------------------------===//
// HSA Code Unit APIs.                                                        //
//===----------------------------------------------------------------------===//

typedef uint64_t hsa_amd_code_unit_t;
typedef uint64_t hsa_amd_code_t;
typedef uint32_t hsa_amd_code_type32_t;
typedef uint32_t hsa_amd_call_convention32_t;
typedef uint8_t hsa_amd_profile8_t;
typedef uint8_t hsa_amd_machine_model8_t;

typedef enum {
  HSA_EXT_CODE_TYPE_NONE = 0,
  HSA_EXT_CODE_TYPE_KERNEL = 1,
  HSA_EXT_CODE_TYPE_INDIRECT_FUNCTION = 2
} hsa_amd_code_type_t;

typedef enum {
  HSA_EXT_CODE_UNIT_INFO_VERSION = 1,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_COUNT = 3,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_CODE = 4,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_CODE_TYPE = 5,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_NAME = 6,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_CALL_CONVENTION = 7,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_GROUP_SEGMENT_SIZE = 8,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_KERNARG_SEGMENT_SIZE = 9,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_PRIVATE_SEGMENT_SIZE = 10,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_PRIVATE_SEGMENT_DYNAMIC_CALL_STACK = 11,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_GROUP_SEGMENT_ALIGNMENT = 15,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_KERNARG_SEGMENT_ALIGNMENT = 16,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_PRIVATE_SEGMENT_ALIGNMENT = 17,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_WAVEFRONT_SIZE = 18,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_WORKGROUP_FBARRIER_COUNT = 19,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_PROFILE = 20,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_MACHINE_MODEL = 21,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_HSAIL_VERSION_MAJOR = 22,
  HSA_EXT_CODE_UNIT_INFO_CODE_ENTITY_HSAIL_VERSION_MINOR = 23,
} hsa_amd_code_unit_info_t;

typedef hsa_status_t (*hsa_ext_symbol_value_callback_t)(
  hsa_runtime_caller_t caller,
  const char *name,
  uint64_t *value);

typedef hsa_status_t (*hsa_ext_code_unit_iterator_callback_t)(
  hsa_runtime_caller_t caller,
  hsa_amd_code_unit_t code_unit);

hsa_status_t HSA_API hsa_ext_code_unit_load(hsa_runtime_caller_t caller,
                                            const hsa_agent_t *agent,
                                            size_t agent_count,
                                            void *serialized_code_unit,
                                            size_t serialized_code_unit_size,
                                            const char *options,
                                            hsa_ext_symbol_value_callback_t symbol_value,
                                            hsa_amd_code_unit_t *code_unit);

hsa_status_t HSA_API hsa_ext_code_unit_destroy(hsa_amd_code_unit_t code_unit);

hsa_status_t HSA_API hsa_ext_code_unit_get_info(hsa_amd_code_unit_t code_unit,
                                                hsa_amd_code_unit_info_t attribute,
                                                uint32_t index,
                                                void *value);

hsa_status_t HSA_API hsa_ext_code_unit_iterator(hsa_runtime_caller_t caller,
                                                hsa_ext_code_unit_iterator_callback_t code_unit_iterator);

#ifdef __cplusplus
}  // end extern "C" block
#endif

#endif  // header guard
