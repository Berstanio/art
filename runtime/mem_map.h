/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright 2014-2016 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_MEM_MAP_H_
#define ART_RUNTIME_MEM_MAP_H_

#include "base/mutex.h"

#include <string>
#include <map>

#include <stddef.h>
#include <sys/mman.h>  // For the PROT_* and MAP_* constants.
#include <sys/types.h>

#include "base/allocator.h"
#include "globals.h"

#ifdef MOE
#include "base/bit_utils.h"
#include <mach/vm_statistics.h>
#endif

namespace art {

#if defined(__LP64__) && (!defined(__x86_64__) || defined(__APPLE__))
#define USE_ART_LOW_4G_ALLOCATOR 1
#else
#define USE_ART_LOW_4G_ALLOCATOR 0
#endif

#ifdef __linux__
static constexpr bool kMadviseZeroes = true;
#else
static constexpr bool kMadviseZeroes = false;
#endif

#ifdef MOE
inline void moeRemapSpace(void* addr, size_t size, int prot, int flags) {
  if (size == 0) {
    return;
  }
  CHECK_ALIGNED(addr, kPageSize);
  CHECK_ALIGNED(size, kPageSize);
  const int tag = VM_MAKE_TAG(VM_MEMORY_APPLICATION_SPECIFIC_1);
  if (mmap(addr, size, prot, flags|MAP_FIXED, tag, 0) != addr) {
    // Fallback to memset if mmap fails
    memset(addr, 0, size);
  }
}
inline int moeFdOrVMTag(int fd, int offs = 0) {
  return fd == -1 ? VM_MAKE_TAG(VM_MEMORY_APPLICATION_SPECIFIC_1 + offs) : fd;
}
#endif

// Used to keep track of mmap segments.
//
// On 64b systems not supporting MAP_32BIT, the implementation of MemMap will do a linear scan
// for free pages. For security, the start of this scan should be randomized. This requires a
// dynamic initializer.
// For this to work, it is paramount that there are no other static initializers that access MemMap.
// Otherwise, calls might see uninitialized values.
class MemMap {
 public:
  // Request an anonymous region of length 'byte_count' and a requested base address.
  // Use null as the requested base address if you don't care.
  // "reuse" allows re-mapping an address range from an existing mapping.
  //
  // The word "anonymous" in this context means "not backed by a file". The supplied
  // 'ashmem_name' will be used -- on systems that support it -- to give the mapping
  // a name.
  //
  // On success, returns returns a MemMap instance.  On failure, returns null.
  static MemMap* MapAnonymous(const char* ashmem_name, uint8_t* addr, size_t byte_count, int prot,
#ifndef MOE
                              bool low_4gb, bool reuse, std::string* error_msg);
#else
                              bool low_4gb, bool reuse, std::string* error_msg,
                              bool preferred = false);
#endif

#ifdef MOE
  static MemMap* MapAlias(const char* name, uint8_t* expected, uint8_t* addr, size_t byte_count,
                          int prot, std::string* error_msg, bool overwrite = false);
#endif

  // Create placeholder for a region allocated by direct call to mmap.
  // This is useful when we do not have control over the code calling mmap,
  // but when we still want to keep track of it in the list.
  // The region is not considered to be owned and will not be unmmaped.
  static MemMap* MapDummy(const char* name, uint8_t* addr, size_t byte_count);

  // Map part of a file, taking care of non-page aligned offsets.  The
  // "start" offset is absolute, not relative.
  //
  // On success, returns returns a MemMap instance.  On failure, returns null.
  static MemMap* MapFile(size_t byte_count, int prot, int flags, int fd, off_t start,
                         const char* filename, std::string* error_msg) {
    return MapFileAtAddress(
        nullptr, byte_count, prot, flags, fd, start, false, filename, error_msg);
  }

  // Map part of a file, taking care of non-page aligned offsets.  The
  // "start" offset is absolute, not relative. This version allows
  // requesting a specific address for the base of the
  // mapping. "reuse" allows us to create a view into an existing
  // mapping where we do not take ownership of the memory.
  //
  // On success, returns returns a MemMap instance.  On failure, returns null.
  static MemMap* MapFileAtAddress(uint8_t* addr, size_t byte_count, int prot, int flags, int fd,
                                  off_t start, bool reuse, const char* filename,
                                  std::string* error_msg);

  // Releases the memory mapping.
  ~MemMap() REQUIRES(!Locks::mem_maps_lock_);

  const std::string& GetName() const {
    return name_;
  }

  bool Protect(int prot);

  void MadviseDontNeedAndZero();

  int GetProtect() const {
    return prot_;
  }

  uint8_t* Begin() const {
    return begin_;
  }

  size_t Size() const {
    return size_;
  }

  // Resize the mem-map by unmapping pages at the end. Currently only supports shrinking.
  void SetSize(size_t new_size);

  uint8_t* End() const {
    return Begin() + Size();
  }

  void* BaseBegin() const {
    return base_begin_;
  }

  size_t BaseSize() const {
    return base_size_;
  }

  void* BaseEnd() const {
    return reinterpret_cast<uint8_t*>(BaseBegin()) + BaseSize();
  }

  bool HasAddress(const void* addr) const {
    return Begin() <= addr && addr < End();
  }

  // Unmap the pages at end and remap them to create another memory map.
  MemMap* RemapAtEnd(uint8_t* new_end, const char* tail_name, int tail_prot,
                     std::string* error_msg);

  static bool CheckNoGaps(MemMap* begin_map, MemMap* end_map)
      REQUIRES(!Locks::mem_maps_lock_);
  static void DumpMaps(std::ostream& os, bool terse = false)
      REQUIRES(!Locks::mem_maps_lock_);

  typedef AllocationTrackingMultiMap<void*, MemMap*, kAllocatorTagMaps> Maps;

  static void Init() REQUIRES(!Locks::mem_maps_lock_);
  static void Shutdown() REQUIRES(!Locks::mem_maps_lock_);

 private:
  MemMap(const std::string& name, uint8_t* begin, size_t size, void* base_begin, size_t base_size,
         int prot, bool reuse, size_t redzone_size = 0) REQUIRES(!Locks::mem_maps_lock_);

  static void DumpMapsLocked(std::ostream& os, bool terse)
      REQUIRES(Locks::mem_maps_lock_);
  static bool HasMemMap(MemMap* map)
      REQUIRES(Locks::mem_maps_lock_);
  static MemMap* GetLargestMemMapAt(void* address)
      REQUIRES(Locks::mem_maps_lock_);
  static bool ContainedWithinExistingMap(uint8_t* ptr, size_t size, std::string* error_msg)
      REQUIRES(!Locks::mem_maps_lock_);

  const std::string name_;
  uint8_t* const begin_;  // Start of data.
  size_t size_;  // Length of data.

  void* const base_begin_;  // Page-aligned base address.
  size_t base_size_;  // Length of mapping. May be changed by RemapAtEnd (ie Zygote).
  int prot_;  // Protection of the map.

#ifdef MOE
  bool alias_;
  bool anon_;
#endif

  // When reuse_ is true, this is just a view of an existing mapping
  // and we do not take ownership and are not responsible for
  // unmapping.
  const bool reuse_;

  const size_t redzone_size_;

#if USE_ART_LOW_4G_ALLOCATOR
  static uintptr_t next_mem_pos_;   // next memory location to check for low_4g extent
#endif

  // All the non-empty MemMaps. Use a multimap as we do a reserve-and-divide (eg ElfMap::Load()).
  static Maps* maps_ GUARDED_BY(Locks::mem_maps_lock_);

  friend class MemMapTest;  // To allow access to base_begin_ and base_size_.
};
std::ostream& operator<<(std::ostream& os, const MemMap& mem_map);
std::ostream& operator<<(std::ostream& os, const MemMap::Maps& mem_maps);

}  // namespace art

#endif  // ART_RUNTIME_MEM_MAP_H_
