/*
 * Copyright (C) 2011 The Android Open Source Project
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

#include "oat_file.h"

#include <dlfcn.h>
#include <string.h>
#include <type_traits>
#include <unistd.h>

#include <cstdlib>
#ifndef __APPLE__
#include <link.h>  // for dl_iterate_phdr.
#endif
#include <sstream>

// dlopen_ext support from bionic.
#ifdef __ANDROID__
#include "android/dlext.h"
#endif

#include "art_method-inl.h"
#include "base/bit_vector.h"
#include "base/stl_util.h"
#include "base/unix_file/fd_file.h"
#ifndef MOE
#include "elf_utils.h"
#else
#include "macho_file.h"
#endif
#include "oat.h"
#include "mem_map.h"
#include "mirror/class.h"
#include "mirror/object-inl.h"
#include "oat_file-inl.h"
#include "oat_file_manager.h"
#include "os.h"
#include "runtime.h"
#include "utils.h"
#include "utils/dex_cache_arrays_layout-inl.h"
#include "vmap_table.h"

namespace art {

// Whether OatFile::Open will try DlOpen() first. Fallback is our own ELF loader.
static constexpr bool kUseDlopen = true;

// Whether OatFile::Open will try DlOpen() on the host. On the host we're not linking against
// bionic, so cannot take advantage of the support for changed semantics (loading the same soname
// multiple times). However, if/when we switch the above, we likely want to switch this, too,
// to get test coverage of the code paths.
static constexpr bool kUseDlopenOnHost = true;

// For debugging, Open will print DlOpen error message if set to true.
static constexpr bool kPrintDlOpenErrorMessage = false;

std::string OatFile::ResolveRelativeEncodedDexLocation(
      const char* abs_dex_location, const std::string& rel_dex_location) {
  if (abs_dex_location != nullptr && rel_dex_location[0] != '/') {
    // Strip :classes<N>.dex used for secondary multidex files.
    std::string base = DexFile::GetBaseLocation(rel_dex_location);
    std::string multidex_suffix = DexFile::GetMultiDexSuffix(rel_dex_location);

    // Check if the base is a suffix of the provided abs_dex_location.
    std::string target_suffix = "/" + base;
    std::string abs_location(abs_dex_location);
    if (abs_location.size() > target_suffix.size()) {
      size_t pos = abs_location.size() - target_suffix.size();
      if (abs_location.compare(pos, std::string::npos, target_suffix) == 0) {
        return abs_location + multidex_suffix;
      }
    }
  }
  return rel_dex_location;
}

void OatFile::CheckLocation(const std::string& location) {
  CHECK(!location.empty());
}

#ifndef MOE
OatFile* OatFile::OpenWithElfFile(ElfFile* elf_file,
                                  const std::string& location,
                                  const char* abs_dex_location,
                                  std::string* error_msg) {
  std::unique_ptr<OatFile> oat_file(new OatFile(location, false));
  oat_file->elf_file_.reset(elf_file);
  uint64_t offset, size;
  bool has_section = elf_file->GetSectionOffsetAndSize(".rodata", &offset, &size);
  CHECK(has_section);
  oat_file->begin_ = elf_file->Begin() + offset;
  oat_file->end_ = elf_file->Begin() + size + offset;
  // Ignore the optional .bss section when opening non-executable.
  return oat_file->Setup(abs_dex_location, error_msg) ? oat_file.release() : nullptr;
}
#endif

OatFile* OatFile::Open(const std::string& filename,
                       const std::string& location,
                       uint8_t* requested_base,
                       uint8_t* oat_file_begin,
                       bool executable,
                       const char* abs_dex_location,
                       std::string* error_msg) {
  CHECK(!filename.empty()) << location;
  CheckLocation(location);
  std::unique_ptr<OatFile> ret;

  // Use dlopen only when flagged to do so, and when it's OK to load things executable.
  // TODO: Also try when not executable? The issue here could be re-mapping as writable (as
  //       !executable is a sign that we may want to patch), which may not be allowed for
  //       various reasons.
  // dlopen always returns the same library if it is already opened on the host. For this reason
  // we only use dlopen if we are the target or we do not already have the dex file opened. Having
  // the same library loaded multiple times at different addresses is required for class unloading
  // and for having dex caches arrays in the .bss section.
#ifndef MOE
  Runtime* const runtime = Runtime::Current();
  OatFileManager* const manager = (runtime != nullptr) ? &runtime->GetOatFileManager() : nullptr;
  if (kUseDlopen && executable) {
    bool success = kIsTargetBuild;
    bool reserved_location = false;
      // Manager may be null if we are running without a runtime.
    if (!success && kUseDlopenOnHost && manager != nullptr) {
      // RegisterOatFileLocation returns false if we are not the first caller to register that
      // location.
      reserved_location = manager->RegisterOatFileLocation(location);
      success = reserved_location;
    }
    if (success) {
      // Try to use dlopen. This may fail for various reasons, outlined below. We try dlopen, as
      // this will register the oat file with the linker and allows libunwind to find our info.
      ret.reset(OpenDlopen(filename, location, requested_base, abs_dex_location, error_msg));
      if (reserved_location) {
        manager->UnRegisterOatFileLocation(location);
      }
      if (ret != nullptr) {
        return ret.release();
      }
      if (kPrintDlOpenErrorMessage) {
        LOG(ERROR) << "Failed to dlopen: " << *error_msg;
      }
    }
  }
#endif

  // If we aren't trying to execute, we just use our own ElfFile loader for a couple reasons:
  //
  // On target, dlopen may fail when compiling due to selinux restrictions on installd.
  //
  // We use our own ELF loader for Quick to deal with legacy apps that
  // open a generated dex file by name, remove the file, then open
  // another generated dex file with the same name. http://b/10614658
  //
  // On host, dlopen is expected to fail when cross compiling, so fall back to OpenElfFile.
  //
  //
  // Another independent reason is the absolute placement of boot.oat. dlopen on the host usually
  // does honor the virtual address encoded in the ELF file only for ET_EXEC files, not ET_DYN.
#ifndef MOE
  std::unique_ptr<File> file(OS::OpenFileForReading(filename.c_str()));
  if (file == nullptr) {
    *error_msg = StringPrintf("Failed to open oat filename for reading: %s", strerror(errno));
    return nullptr;
  }
  ret.reset(OpenElfFile(file.get(), location, requested_base, oat_file_begin, false, executable,
                        abs_dex_location, error_msg));
#else
  ret.reset(OpenThisDlopen(requested_base, error_msg));
#endif

  // It would be nice to unlink here. But we might have opened the file created by the
  // ScopedLock, which we better not delete to avoid races. TODO: Investigate how to fix the API
  // to allow removal when we know the ELF must be borked.
  return ret.release();
}

OatFile* OatFile::OpenWritable(File* file, const std::string& location,
                               const char* abs_dex_location,
                               std::string* error_msg) {
  CheckLocation(location);
  return OpenElfFile(file, location, nullptr, nullptr, true, false, abs_dex_location, error_msg);
}

OatFile* OatFile::OpenReadable(File* file, const std::string& location,
                               const char* abs_dex_location,
                               std::string* error_msg) {
  CheckLocation(location);
  return OpenElfFile(file, location, nullptr, nullptr, false, false, abs_dex_location, error_msg);
}

OatFile* OatFile::OpenDlopen(const std::string& elf_filename,
                             const std::string& location,
                             uint8_t* requested_base,
                             const char* abs_dex_location,
                             std::string* error_msg) {
  std::unique_ptr<OatFile> oat_file(new OatFile(location, true));
  bool success = oat_file->Dlopen(elf_filename, requested_base, abs_dex_location, error_msg);
  if (!success) {
    return nullptr;
  }
  return oat_file.release();
}

#ifdef MOE
OatFile* OatFile::OpenThisDlopen(uint8_t* requested_base, std::string* error_msg) {
    if (requested_base == nullptr) {
        return nullptr;
    }
    std::unique_ptr<OatFile> oat_file(new OatFile("oatdata_symbol", true));
    bool success = oat_file->ThisDlopen(requested_base, error_msg);
    if (!success) {
        return nullptr;
    }
    return oat_file.release();
}
#endif

OatFile* OatFile::OpenElfFile(File* file,
                              const std::string& location,
                              uint8_t* requested_base,
                              uint8_t* oat_file_begin,
                              bool writable,
                              bool executable,
                              const char* abs_dex_location,
                              std::string* error_msg) {
  std::unique_ptr<OatFile> oat_file(new OatFile(location, executable));
  bool success = oat_file->ElfFileOpen(file, requested_base, oat_file_begin, writable, executable,
                                       abs_dex_location, error_msg);
  if (!success) {
    CHECK(!error_msg->empty());
    return nullptr;
  }
  return oat_file.release();
}

OatFile::OatFile(const std::string& location, bool is_executable)
    : location_(location), begin_(nullptr), end_(nullptr), bss_begin_(nullptr), bss_end_(nullptr),
      is_executable_(is_executable), dlopen_handle_(nullptr),
      secondary_lookup_lock_("OatFile secondary lookup lock", kOatFileSecondaryLookupLock) {
  CHECK(!location_.empty());
  Runtime* const runtime = Runtime::Current();
  if (runtime != nullptr && !runtime->IsAotCompiler()) {
    runtime->GetOatFileManager().RegisterOatFileLocation(location);
  }
}

OatFile::~OatFile() {
  STLDeleteElements(&oat_dex_files_storage_);
  if (dlopen_handle_ != nullptr) {
    dlclose(dlopen_handle_);
  }
  Runtime* const runtime = Runtime::Current();
  if (runtime != nullptr && !runtime->IsAotCompiler()) {
    runtime->GetOatFileManager().UnRegisterOatFileLocation(location_);
  }
}

#ifdef MOE
static inline MemMap* get_oat_data_map(uint8_t* addr,  std::string* error_msg) {
  size_t size;
  void* slided = get_oat_data(&size);
  if (!slided) {
    return nullptr;
  }

  return MemMap::MapAlias("__oatdata_alias", addr, reinterpret_cast<uint8_t*>(slided), size, PROT_READ | PROT_EXEC, error_msg, true);
}

bool OatFile::ThisDlopen(uint8_t* requested_base, std::string* error_msg) {
  mem_map_.reset(get_oat_data_map(requested_base, error_msg));
  if (mem_map_.get() == nullptr) {
    LOG(WARNING) << "Failed to find oatdata symbol in executable" << ": " << dlerror();
    return false;
  }

  begin_ = mem_map_->Begin();
  end_ = mem_map_->End();

  if (requested_base != nullptr && begin_ != requested_base) {
    std::string maps;
    LOG(WARNING) << "Failed to find oatdata symbol at expected address: oatdata="
    << reinterpret_cast<const void*>(begin_) << " != expected="
    << reinterpret_cast<const void*>(requested_base) << "\n";
    return false;
  }

  return Setup(nullptr, error_msg);
}
#endif

bool OatFile::Dlopen(const std::string& elf_filename, uint8_t* requested_base,
                     const char* abs_dex_location, std::string* error_msg) {
#ifdef __APPLE__
  // The dl_iterate_phdr syscall is missing.  There is similar API on OSX,
  // but let's fallback to the custom loading code for the time being.
  UNUSED(elf_filename, requested_base, abs_dex_location, error_msg);
  return false;
#else
  {
    UniqueCPtr<char> absolute_path(realpath(elf_filename.c_str(), nullptr));
    if (absolute_path == nullptr) {
      *error_msg = StringPrintf("Failed to find absolute path for '%s'", elf_filename.c_str());
      return false;
    }
#ifdef __ANDROID__
    android_dlextinfo extinfo;
    extinfo.flags = ANDROID_DLEXT_FORCE_LOAD | ANDROID_DLEXT_FORCE_FIXED_VADDR;
    dlopen_handle_ = android_dlopen_ext(absolute_path.get(), RTLD_NOW, &extinfo);
#else
    dlopen_handle_ = dlopen(absolute_path.get(), RTLD_NOW);
#endif
  }
  if (dlopen_handle_ == nullptr) {
    *error_msg = StringPrintf("Failed to dlopen '%s': %s", elf_filename.c_str(), dlerror());
    return false;
  }
  begin_ = reinterpret_cast<uint8_t*>(dlsym(dlopen_handle_, "oatdata"));
  if (begin_ == nullptr) {
    *error_msg = StringPrintf("Failed to find oatdata symbol in '%s': %s", elf_filename.c_str(),
                              dlerror());
    return false;
  }
  if (requested_base != nullptr && begin_ != requested_base) {
    PrintFileToLog("/proc/self/maps", LogSeverity::WARNING);
    *error_msg = StringPrintf("Failed to find oatdata symbol at expected address: "
                              "oatdata=%p != expected=%p, %s. See process maps in the log.",
                              begin_, requested_base, elf_filename.c_str());
    return false;
  }
  end_ = reinterpret_cast<uint8_t*>(dlsym(dlopen_handle_, "oatlastword"));
  if (end_ == nullptr) {
    *error_msg = StringPrintf("Failed to find oatlastword symbol in '%s': %s", elf_filename.c_str(),
                              dlerror());
    return false;
  }
  // Readjust to be non-inclusive upper bound.
  end_ += sizeof(uint32_t);

  bss_begin_ = reinterpret_cast<uint8_t*>(dlsym(dlopen_handle_, "oatbss"));
  if (bss_begin_ == nullptr) {
    // No .bss section. Clear dlerror().
    bss_end_ = nullptr;
    dlerror();
  } else {
    bss_end_ = reinterpret_cast<uint8_t*>(dlsym(dlopen_handle_, "oatbsslastword"));
    if (bss_end_ == nullptr) {
      *error_msg = StringPrintf("Failed to find oatbasslastword symbol in '%s'",
                                elf_filename.c_str());
      return false;
    }
    // Readjust to be non-inclusive upper bound.
    bss_end_ += sizeof(uint32_t);
  }

  // Ask the linker where it mmaped the file and notify our mmap wrapper of the regions.
  struct dl_iterate_context {
    static int callback(struct dl_phdr_info *info, size_t /* size */, void *data) {
      auto* context = reinterpret_cast<dl_iterate_context*>(data);
      // See whether this callback corresponds to the file which we have just loaded.
      bool contains_begin = false;
      for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
          uint8_t* vaddr = reinterpret_cast<uint8_t*>(info->dlpi_addr +
                                                      info->dlpi_phdr[i].p_vaddr);
          size_t memsz = info->dlpi_phdr[i].p_memsz;
          if (vaddr <= context->begin_ && context->begin_ < vaddr + memsz) {
            contains_begin = true;
            break;
          }
        }
      }
      // Add dummy mmaps for this file.
      if (contains_begin) {
        for (int i = 0; i < info->dlpi_phnum; i++) {
          if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            uint8_t* vaddr = reinterpret_cast<uint8_t*>(info->dlpi_addr +
                                                        info->dlpi_phdr[i].p_vaddr);
            size_t memsz = info->dlpi_phdr[i].p_memsz;
            MemMap* mmap = MemMap::MapDummy(info->dlpi_name, vaddr, memsz);
            context->dlopen_mmaps_->push_back(std::unique_ptr<MemMap>(mmap));
          }
        }
        return 1;  // Stop iteration and return 1 from dl_iterate_phdr.
      }
      return 0;  // Continue iteration and return 0 from dl_iterate_phdr when finished.
    }
    const uint8_t* const begin_;
    std::vector<std::unique_ptr<MemMap>>* const dlopen_mmaps_;
  } context = { begin_, &dlopen_mmaps_ };

  if (dl_iterate_phdr(dl_iterate_context::callback, &context) == 0) {
    PrintFileToLog("/proc/self/maps", LogSeverity::WARNING);
    LOG(ERROR) << "File " << elf_filename << " loaded with dlopen but can not find its mmaps.";
  }

  return Setup(abs_dex_location, error_msg);
#endif  // __APPLE__
}

bool OatFile::ElfFileOpen(File* file, uint8_t* requested_base, uint8_t* oat_file_begin,
                          bool writable, bool executable,
                          const char* abs_dex_location,
                          std::string* error_msg) {
#ifndef MOE
  // TODO: rename requested_base to oat_data_begin
  elf_file_.reset(ElfFile::Open(file, writable, /*program_header_only*/true, error_msg,
                                oat_file_begin));
  if (elf_file_ == nullptr) {
#else
  macho_file_.reset(MachOFile::Open(file, writable, error_msg));
  if (macho_file_ == nullptr) {
#endif
    DCHECK(!error_msg->empty());
    return false;
  }
#ifndef MOE
  bool loaded = elf_file_->Load(executable, error_msg);
  if (!loaded) {
    DCHECK(!error_msg->empty());
    return false;
  }
#endif
#ifndef MOE
  begin_ = elf_file_->FindDynamicSymbolAddress("oatdata");
#else
  begin_ = macho_file_->Begin();
#endif
  if (begin_ == nullptr) {
    *error_msg = StringPrintf("Failed to find oatdata symbol in '%s'", file->GetPath().c_str());
    return false;
  }
  if (requested_base != nullptr && begin_ != requested_base) {
    PrintFileToLog("/proc/self/maps", LogSeverity::WARNING);
    *error_msg = StringPrintf("Failed to find oatdata symbol at expected address: "
                              "oatdata=%p != expected=%p. See process maps in the log.",
                              begin_, requested_base);
    return false;
  }
#ifndef MOE
  end_ = elf_file_->FindDynamicSymbolAddress("oatlastword");
  if (end_ == nullptr) {
    *error_msg = StringPrintf("Failed to find oatlastword symbol in '%s'", file->GetPath().c_str());
    return false;
  }
  // Readjust to be non-inclusive upper bound.
  end_ += sizeof(uint32_t);

  bss_begin_ = const_cast<uint8_t*>(elf_file_->FindDynamicSymbolAddress("oatbss"));
  if (bss_begin_ == nullptr) {
    // No .bss section. Clear dlerror().
    bss_end_ = nullptr;
    dlerror();
  } else {
    bss_end_ = const_cast<uint8_t*>(elf_file_->FindDynamicSymbolAddress("oatbsslastword"));
    if (bss_end_ == nullptr) {
      *error_msg = StringPrintf("Failed to find oatbasslastword symbol in '%s'",
                                file->GetPath().c_str());
      return false;
    }
    // Readjust to be non-inclusive upper bound.
    bss_end_ += sizeof(uint32_t);
  }
#else
  end_ = begin_ + file->GetLength();
#endif

  return Setup(abs_dex_location, error_msg);
}

// Read an unaligned entry from the OatDexFile data in OatFile and advance the read
// position by the number of bytes read, i.e. sizeof(T).
// Return true on success, false if the read would go beyond the end of the OatFile.
template <typename T>
inline static bool ReadOatDexFileData(const OatFile& oat_file,
                                      /*inout*/const uint8_t** oat,
                                      /*out*/T* value) {
  DCHECK(oat != nullptr);
  DCHECK(value != nullptr);
  DCHECK_LE(*oat, oat_file.End());
  if (UNLIKELY(static_cast<size_t>(oat_file.End() - *oat) < sizeof(T))) {
    return false;
  }
  static_assert(std::is_trivial<T>::value, "T must be a trivial type");
  typedef __attribute__((__aligned__(1))) T unaligned_type;
  *value = *reinterpret_cast<const unaligned_type*>(*oat);
  *oat += sizeof(T);
  return true;
}

bool OatFile::Setup(const char* abs_dex_location, std::string* error_msg) {
  if (!GetOatHeader().IsValid()) {
    std::string cause = GetOatHeader().GetValidationErrorMessage();
    *error_msg = StringPrintf("Invalid oat header for '%s': %s",
                              GetLocation().c_str(),
                              cause.c_str());
    return false;
  }
  const uint8_t* oat = Begin();
  oat += sizeof(OatHeader);
  if (oat > End()) {
    *error_msg = StringPrintf("In oat file '%s' found truncated OatHeader", GetLocation().c_str());
    return false;
  }

  oat += GetOatHeader().GetKeyValueStoreSize();
  if (oat > End()) {
    *error_msg = StringPrintf("In oat file '%s' found truncated variable-size data: "
                                  "%p + %zu + %u <= %p",
                              GetLocation().c_str(),
                              Begin(),
                              sizeof(OatHeader),
                              GetOatHeader().GetKeyValueStoreSize(),
                              End());
    return false;
  }

  size_t pointer_size = GetInstructionSetPointerSize(GetOatHeader().GetInstructionSet());
  uint8_t* dex_cache_arrays = bss_begin_;
  uint32_t dex_file_count = GetOatHeader().GetDexFileCount();
  oat_dex_files_storage_.reserve(dex_file_count);
  for (size_t i = 0; i < dex_file_count; i++) {
    uint32_t dex_file_location_size;
    if (UNLIKELY(!ReadOatDexFileData(*this, &oat, &dex_file_location_size))) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu truncated after dex file "
                                    "location size",
                                GetLocation().c_str(),
                                i);
      return false;
    }
    if (UNLIKELY(dex_file_location_size == 0U)) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu with empty location name",
                                GetLocation().c_str(),
                                i);
      return false;
    }

    const char* dex_file_location_data = reinterpret_cast<const char*>(oat);
    oat += dex_file_location_size;
    if (UNLIKELY(oat > End())) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu with truncated dex file "
                                    "location",
                                GetLocation().c_str(),
                                i);
      return false;
    }

    std::string dex_file_location = ResolveRelativeEncodedDexLocation(
        abs_dex_location,
        std::string(dex_file_location_data, dex_file_location_size));

    uint32_t dex_file_checksum;
    if (UNLIKELY(!ReadOatDexFileData(*this, &oat, &dex_file_checksum))) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' truncated after "
                                    "dex file checksum",
                                GetLocation().c_str(),
                                i,
                                dex_file_location.c_str());
      return false;
    }

    uint32_t dex_file_offset;
    if (UNLIKELY(!ReadOatDexFileData(*this, &oat, &dex_file_offset))) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' truncated "
                                    "after dex file offsets",
                                GetLocation().c_str(),
                                i,
                                dex_file_location.c_str());
      return false;
    }
    if (UNLIKELY(dex_file_offset == 0U)) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' with zero dex "
                                    "file offset",
                                GetLocation().c_str(),
                                i,
                                dex_file_location.c_str());
      return false;
    }
    if (UNLIKELY(dex_file_offset > Size())) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' with dex file "
                                    "offset %u > %zu",
                                GetLocation().c_str(),
                                i,
                                dex_file_location.c_str(),
                                dex_file_offset,
                                Size());
      return false;
    }

    const uint8_t* dex_file_pointer = Begin() + dex_file_offset;
    if (UNLIKELY(!DexFile::IsMagicValid(dex_file_pointer))) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' with invalid "
                                    "dex file magic '%s'",
                                GetLocation().c_str(),
                                i,
                                dex_file_location.c_str(),
                                dex_file_pointer);
      return false;
    }
    if (UNLIKELY(!DexFile::IsVersionValid(dex_file_pointer))) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' with invalid "
                                    "dex file version '%s'",
                                GetLocation().c_str(),
                                i,
                                dex_file_location.c_str(),
                                dex_file_pointer);
      return false;
    }
    const DexFile::Header* header = reinterpret_cast<const DexFile::Header*>(dex_file_pointer);
    const uint32_t* methods_offsets_pointer = reinterpret_cast<const uint32_t*>(oat);

    oat += (sizeof(*methods_offsets_pointer) * header->class_defs_size_);
    if (UNLIKELY(oat > End())) {
      *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' with truncated "
                                    "method offsets",
                                GetLocation().c_str(),
                                i,
                                dex_file_location.c_str());
      return false;
    }

    uint8_t* current_dex_cache_arrays = nullptr;
    if (dex_cache_arrays != nullptr) {
      DexCacheArraysLayout layout(pointer_size, *header);
      if (layout.Size() != 0u) {
        if (static_cast<size_t>(bss_end_ - dex_cache_arrays) < layout.Size()) {
          *error_msg = StringPrintf("In oat file '%s' found OatDexFile #%zu for '%s' with "
                                        "truncated dex cache arrays, %zu < %zu.",
                                    GetLocation().c_str(),
                                    i,
                                    dex_file_location.c_str(),
                                    static_cast<size_t>(bss_end_ - dex_cache_arrays),
                                    layout.Size());
          return false;
        }
        current_dex_cache_arrays = dex_cache_arrays;
        dex_cache_arrays += layout.Size();
      }
    }

    std::string canonical_location = DexFile::GetDexCanonicalLocation(dex_file_location.c_str());

    // Create the OatDexFile and add it to the owning container.
    OatDexFile* oat_dex_file = new OatDexFile(this,
                                              dex_file_location,
                                              canonical_location,
                                              dex_file_checksum,
                                              dex_file_pointer,
                                              methods_offsets_pointer,
                                              current_dex_cache_arrays);
    oat_dex_files_storage_.push_back(oat_dex_file);

    // Add the location and canonical location (if different) to the oat_dex_files_ table.
    StringPiece key(oat_dex_file->GetDexFileLocation());
    oat_dex_files_.Put(key, oat_dex_file);
    if (canonical_location != dex_file_location) {
      StringPiece canonical_key(oat_dex_file->GetCanonicalDexFileLocation());
      oat_dex_files_.Put(canonical_key, oat_dex_file);
    }
  }

  if (dex_cache_arrays != bss_end_) {
    // We expect the bss section to be either empty (dex_cache_arrays and bss_end_
    // both null) or contain just the dex cache arrays and nothing else.
    *error_msg = StringPrintf("In oat file '%s' found unexpected bss size bigger by %zu bytes.",
                              GetLocation().c_str(),
                              static_cast<size_t>(bss_end_ - dex_cache_arrays));
    return false;
  }
  return true;
}

const OatHeader& OatFile::GetOatHeader() const {
  return *reinterpret_cast<const OatHeader*>(Begin());
}

const uint8_t* OatFile::Begin() const {
  CHECK(begin_ != nullptr);
  return begin_;
}

const uint8_t* OatFile::End() const {
  CHECK(end_ != nullptr);
  return end_;
}

const uint8_t* OatFile::BssBegin() const {
  return bss_begin_;
}

const uint8_t* OatFile::BssEnd() const {
  return bss_end_;
}

const OatFile::OatDexFile* OatFile::GetOatDexFile(const char* dex_location,
                                                  const uint32_t* dex_location_checksum,
                                                  bool warn_if_not_found) const {
  // NOTE: We assume here that the canonical location for a given dex_location never
  // changes. If it does (i.e. some symlink used by the filename changes) we may return
  // an incorrect OatDexFile. As long as we have a checksum to check, we shall return
  // an identical file or fail; otherwise we may see some unpredictable failures.

  // TODO: Additional analysis of usage patterns to see if this can be simplified
  // without any performance loss, for example by not doing the first lock-free lookup.

  const OatFile::OatDexFile* oat_dex_file = nullptr;
  StringPiece key(dex_location);
  // Try to find the key cheaply in the oat_dex_files_ map which holds dex locations
  // directly mentioned in the oat file and doesn't require locking.
  auto primary_it = oat_dex_files_.find(key);
  if (primary_it != oat_dex_files_.end()) {
    oat_dex_file = primary_it->second;
    DCHECK(oat_dex_file != nullptr);
  } else {
    // This dex_location is not one of the dex locations directly mentioned in the
    // oat file. The correct lookup is via the canonical location but first see in
    // the secondary_oat_dex_files_ whether we've looked up this location before.
    MutexLock mu(Thread::Current(), secondary_lookup_lock_);
    auto secondary_lb = secondary_oat_dex_files_.lower_bound(key);
    if (secondary_lb != secondary_oat_dex_files_.end() && key == secondary_lb->first) {
      oat_dex_file = secondary_lb->second;  // May be null.
    } else {
      // We haven't seen this dex_location before, we must check the canonical location.
      std::string dex_canonical_location = DexFile::GetDexCanonicalLocation(dex_location);
      if (dex_canonical_location != dex_location) {
        StringPiece canonical_key(dex_canonical_location);
        auto canonical_it = oat_dex_files_.find(canonical_key);
        if (canonical_it != oat_dex_files_.end()) {
          oat_dex_file = canonical_it->second;
        }  // else keep null.
      }  // else keep null.

      // Copy the key to the string_cache_ and store the result in secondary map.
      string_cache_.emplace_back(key.data(), key.length());
      StringPiece key_copy(string_cache_.back());
      secondary_oat_dex_files_.PutBefore(secondary_lb, key_copy, oat_dex_file);
    }
  }
  if (oat_dex_file != nullptr &&
      (dex_location_checksum == nullptr ||
       oat_dex_file->GetDexFileLocationChecksum() == *dex_location_checksum)) {
    return oat_dex_file;
  }

  if (warn_if_not_found) {
    std::string dex_canonical_location = DexFile::GetDexCanonicalLocation(dex_location);
    std::string checksum("<unspecified>");
    if (dex_location_checksum != nullptr) {
      checksum = StringPrintf("0x%08x", *dex_location_checksum);
    }
    LOG(WARNING) << "Failed to find OatDexFile for DexFile " << dex_location
                 << " ( canonical path " << dex_canonical_location << ")"
                 << " with checksum " << checksum << " in OatFile " << GetLocation();
    if (kIsDebugBuild) {
      for (const OatDexFile* odf : oat_dex_files_storage_) {
        LOG(WARNING) << "OatFile " << GetLocation()
                     << " contains OatDexFile " << odf->GetDexFileLocation()
                     << " (canonical path " << odf->GetCanonicalDexFileLocation() << ")"
                     << " with checksum 0x" << std::hex << odf->GetDexFileLocationChecksum();
      }
    }
  }

  return nullptr;
}

OatFile::OatDexFile::OatDexFile(const OatFile* oat_file,
                                const std::string& dex_file_location,
                                const std::string& canonical_dex_file_location,
                                uint32_t dex_file_location_checksum,
                                const uint8_t* dex_file_pointer,
                                const uint32_t* oat_class_offsets_pointer,
                                uint8_t* dex_cache_arrays)
    : oat_file_(oat_file),
      dex_file_location_(dex_file_location),
      canonical_dex_file_location_(canonical_dex_file_location),
      dex_file_location_checksum_(dex_file_location_checksum),
      dex_file_pointer_(dex_file_pointer),
      oat_class_offsets_pointer_(oat_class_offsets_pointer),
      dex_cache_arrays_(dex_cache_arrays) {}

art::OatDexFile::~OatDexFile() {}

size_t OatFile::OatDexFile::FileSize() const {
  return reinterpret_cast<const DexFile::Header*>(dex_file_pointer_)->file_size_;
}

std::unique_ptr<const DexFile> OatFile::OatDexFile::OpenDexFile(std::string* error_msg) const {
  return DexFile::Open(dex_file_pointer_, FileSize(), dex_file_location_,
                       dex_file_location_checksum_, this, error_msg);
}

uint32_t OatFile::OatDexFile::GetOatClassOffset(uint16_t class_def_index) const {
  return oat_class_offsets_pointer_[class_def_index];
}

OatFile::OatClass OatFile::OatDexFile::GetOatClass(uint16_t class_def_index) const {
  uint32_t oat_class_offset = GetOatClassOffset(class_def_index);

  const uint8_t* oat_class_pointer = oat_file_->Begin() + oat_class_offset;
  CHECK_LT(oat_class_pointer, oat_file_->End()) << oat_file_->GetLocation();

  const uint8_t* status_pointer = oat_class_pointer;
  CHECK_LT(status_pointer, oat_file_->End()) << oat_file_->GetLocation();
  mirror::Class::Status status =
      static_cast<mirror::Class::Status>(*reinterpret_cast<const int16_t*>(status_pointer));
  CHECK_LT(status, mirror::Class::kStatusMax);

  const uint8_t* type_pointer = status_pointer + sizeof(uint16_t);
  CHECK_LT(type_pointer, oat_file_->End()) << oat_file_->GetLocation();
  OatClassType type = static_cast<OatClassType>(*reinterpret_cast<const uint16_t*>(type_pointer));
  CHECK_LT(type, kOatClassMax);

  const uint8_t* after_type_pointer = type_pointer + sizeof(int16_t);
  CHECK_LE(after_type_pointer, oat_file_->End()) << oat_file_->GetLocation();

  uint32_t bitmap_size = 0;
  const uint8_t* bitmap_pointer = nullptr;
  const uint8_t* methods_pointer = nullptr;
  if (type != kOatClassNoneCompiled) {
    if (type == kOatClassSomeCompiled) {
      bitmap_size = static_cast<uint32_t>(*reinterpret_cast<const uint32_t*>(after_type_pointer));
      bitmap_pointer = after_type_pointer + sizeof(bitmap_size);
      CHECK_LE(bitmap_pointer, oat_file_->End()) << oat_file_->GetLocation();
      methods_pointer = bitmap_pointer + bitmap_size;
    } else {
      methods_pointer = after_type_pointer;
    }
    CHECK_LE(methods_pointer, oat_file_->End()) << oat_file_->GetLocation();
  }

  return OatFile::OatClass(oat_file_,
                           status,
                           type,
                           bitmap_size,
                           reinterpret_cast<const uint32_t*>(bitmap_pointer),
                           reinterpret_cast<const OatMethodOffsets*>(methods_pointer));
}

OatFile::OatClass::OatClass(const OatFile* oat_file,
                            mirror::Class::Status status,
                            OatClassType type,
                            uint32_t bitmap_size,
                            const uint32_t* bitmap_pointer,
                            const OatMethodOffsets* methods_pointer)
    : oat_file_(oat_file), status_(status), type_(type),
      bitmap_(bitmap_pointer), methods_pointer_(methods_pointer) {
    switch (type_) {
      case kOatClassAllCompiled: {
        CHECK_EQ(0U, bitmap_size);
        CHECK(bitmap_pointer == nullptr);
        CHECK(methods_pointer != nullptr);
        break;
      }
      case kOatClassSomeCompiled: {
        CHECK_NE(0U, bitmap_size);
        CHECK(bitmap_pointer != nullptr);
        CHECK(methods_pointer != nullptr);
        break;
      }
      case kOatClassNoneCompiled: {
        CHECK_EQ(0U, bitmap_size);
        CHECK(bitmap_pointer == nullptr);
        CHECK(methods_pointer_ == nullptr);
        break;
      }
      case kOatClassMax: {
        LOG(FATAL) << "Invalid OatClassType " << type_;
        break;
      }
    }
}

uint32_t OatFile::OatClass::GetOatMethodOffsetsOffset(uint32_t method_index) const {
  const OatMethodOffsets* oat_method_offsets = GetOatMethodOffsets(method_index);
  if (oat_method_offsets == nullptr) {
    return 0u;
  }
  return reinterpret_cast<const uint8_t*>(oat_method_offsets) - oat_file_->Begin();
}

const OatMethodOffsets* OatFile::OatClass::GetOatMethodOffsets(uint32_t method_index) const {
  // NOTE: We don't keep the number of methods and cannot do a bounds check for method_index.
  if (methods_pointer_ == nullptr) {
    CHECK_EQ(kOatClassNoneCompiled, type_);
    return nullptr;
  }
  size_t methods_pointer_index;
  if (bitmap_ == nullptr) {
    CHECK_EQ(kOatClassAllCompiled, type_);
    methods_pointer_index = method_index;
  } else {
    CHECK_EQ(kOatClassSomeCompiled, type_);
    if (!BitVector::IsBitSet(bitmap_, method_index)) {
      return nullptr;
    }
    size_t num_set_bits = BitVector::NumSetBits(bitmap_, method_index);
    methods_pointer_index = num_set_bits;
  }
  const OatMethodOffsets& oat_method_offsets = methods_pointer_[methods_pointer_index];
  return &oat_method_offsets;
}

const OatFile::OatMethod OatFile::OatClass::GetOatMethod(uint32_t method_index) const {
  const OatMethodOffsets* oat_method_offsets = GetOatMethodOffsets(method_index);
  if (oat_method_offsets == nullptr) {
    return OatMethod(nullptr, 0);
  }
  if (oat_file_->IsExecutable() ||
      Runtime::Current() == nullptr ||        // This case applies for oatdump.
      Runtime::Current()->IsAotCompiler()) {
    return OatMethod(oat_file_->Begin(), oat_method_offsets->code_offset_);
  }
  // We aren't allowed to use the compiled code. We just force it down the interpreted / jit
  // version.
  return OatMethod(oat_file_->Begin(), 0);
}

void OatFile::OatMethod::LinkMethod(ArtMethod* method) const {
  CHECK(method != nullptr);
  method->SetEntryPointFromQuickCompiledCode(GetQuickCode());
}

bool OatFile::IsPic() const {
  return GetOatHeader().IsPic();
  // TODO: Check against oat_patches. b/18144996
}

bool OatFile::IsDebuggable() const {
  return GetOatHeader().IsDebuggable();
}

static constexpr char kDexClassPathEncodingSeparator = '*';

std::string OatFile::EncodeDexFileDependencies(const std::vector<const DexFile*>& dex_files) {
  std::ostringstream out;

  for (const DexFile* dex_file : dex_files) {
    out << dex_file->GetLocation().c_str();
    out << kDexClassPathEncodingSeparator;
    out << dex_file->GetLocationChecksum();
    out << kDexClassPathEncodingSeparator;
  }

  return out.str();
}

bool OatFile::CheckStaticDexFileDependencies(const char* dex_dependencies, std::string* msg) {
  if (dex_dependencies == nullptr || dex_dependencies[0] == 0) {
    // No dependencies.
    return true;
  }

  // Assumption: this is not performance-critical. So it's OK to do this with a std::string and
  //             Split() instead of manual parsing of the combined char*.
  std::vector<std::string> split;
  Split(dex_dependencies, kDexClassPathEncodingSeparator, &split);
  if (split.size() % 2 != 0) {
    // Expected pairs of location and checksum.
    *msg = StringPrintf("Odd number of elements in dependency list %s", dex_dependencies);
    return false;
  }

  for (auto it = split.begin(), end = split.end(); it != end; it += 2) {
    std::string& location = *it;
    std::string& checksum = *(it + 1);
    int64_t converted = strtoll(checksum.c_str(), nullptr, 10);
    if (converted == 0) {
      // Conversion error.
      *msg = StringPrintf("Conversion error for %s", checksum.c_str());
      return false;
    }

    uint32_t dex_checksum;
    std::string error_msg;
    if (DexFile::GetChecksum(DexFile::GetDexCanonicalLocation(location.c_str()).c_str(),
                             &dex_checksum,
                             &error_msg)) {
      if (converted != dex_checksum) {
        *msg = StringPrintf("Checksums don't match for %s: %" PRId64 " vs %u",
                            location.c_str(), converted, dex_checksum);
        return false;
      }
    } else {
      // Problem retrieving checksum.
      // TODO: odex files?
      *msg = StringPrintf("Could not retrieve checksum for %s: %s", location.c_str(),
                          error_msg.c_str());
      return false;
    }
  }

  return true;
}

bool OatFile::GetDexLocationsFromDependencies(const char* dex_dependencies,
                                              std::vector<std::string>* locations) {
  DCHECK(locations != nullptr);
  if (dex_dependencies == nullptr || dex_dependencies[0] == 0) {
    return true;
  }

  // Assumption: this is not performance-critical. So it's OK to do this with a std::string and
  //             Split() instead of manual parsing of the combined char*.
  std::vector<std::string> split;
  Split(dex_dependencies, kDexClassPathEncodingSeparator, &split);
  if (split.size() % 2 != 0) {
    // Expected pairs of location and checksum.
    return false;
  }

  for (auto it = split.begin(), end = split.end(); it != end; it += 2) {
    locations->push_back(*it);
  }

  return true;
}

}  // namespace art
