// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/bin/zxdb/symbols/resolve_options.h"
#include "garnet/bin/zxdb/symbols/system_symbols.h"
#include "garnet/public/lib/fxl/macros.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace debug_ipc {
struct Module;
}

namespace zxdb {

struct InputLocation;
class LineDetails;
class LoadedModuleSymbols;
struct ModuleSymbolStatus;
struct ResolveOptions;
class TargetSymbols;

// Main client interface for querying process symbol information. This requires
// a running process and returns real addresses. See also TargetSymbols.
//
// This class is a collection for modules. As such, it's not very useful to
// mock. Instead, mock the ModuleSymbols and add them to this ProcessSymbols
// class (see ProcessSymbolsTestSetup helper class).
class ProcessSymbols {
 public:
  // A simple observer interface. This allows ProcessImpl to expose these
  // in the ProcessObserver observer API. If the API here gets too much more
  // complicated, it could be we want a separate public ProcessSymbolsObserver
  // class that consumers need to register for explicitly.
  //
  // See the corresponding functions in ProcessObserver for docs.
  class Notifications {
   public:
    virtual void DidLoadModuleSymbols(LoadedModuleSymbols* module) {}
    virtual void WillUnloadModuleSymbols(LoadedModuleSymbols* module) {}
    virtual void OnSymbolLoadFailure(const Err& err) {}
  };

  // The passed-in pointers must outlive this class.
  ProcessSymbols(Notifications* notifications, TargetSymbols* target_symbols);
  ~ProcessSymbols();

  fxl::WeakPtr<const ProcessSymbols> GetWeakPtr() const;

  TargetSymbols* target_symbols() { return target_symbols_; }

  // Replaces all modules with the given list.
  void SetModules(const std::vector<debug_ipc::Module>& modules);

  // Appends the ModuleSymbols implementation to the current list (unlike
  // SetModules which does a replacement). This is typically used to populate
  // a ProcessSymbols with one or more MockModuleSymbols for testing purposes.
  void InjectModuleForTesting(const std::string& name,
                              const std::string& build_id,
                              std::unique_ptr<LoadedModuleSymbols> mod_sym);

  // Returns statistics on the currently-loaded modules.
  std::vector<ModuleSymbolStatus> GetStatus() const;

  // Returns the information for all the modules that were loaded with
  // symbol information.
  std::vector<const LoadedModuleSymbols*> GetLoadedModuleSymbols() const;

  // Converts the given InputLocation into one or more locations. The input
  // can match zero, one, or many locations.
  //
  // If symbolize is true, the results will be symbolized, otherwise the
  // output locations will be regular addresses (this will be slightly faster).
  std::vector<Location> ResolveInputLocation(
      const InputLocation& input_location,
      const ResolveOptions& options = ResolveOptions()) const;

  // Computes the line that corresponds to the given address. Unlike
  // ResolveInputLocation (which just returns the current source line), this
  // returns the entire set of contiguous line table entries with code ranges
  // with the same line as the given address.
  LineDetails LineDetailsForAddress(uint64_t address) const;

  // Returns true if the code location is inside a module where there are
  // symbols loaded. If we did something like index ELF exports, those wouldn't
  // count. "Symbols loaded" here means there is real DWARF debugging
  // information available.
  bool HaveSymbolsLoadedForModuleAt(uint64_t address) const;

 private:
  struct ModuleInfo {
    std::string name;
    std::string build_id;
    uint64_t base = 0;

    // MAY BE NULL if the symbols could not be loaded.
    //
    // If this is ever extended to exist even if DWARF symbols can not be
    // loaded (like we index ELF exports), be sure to update
    // HaveSymbolsLoadedForModuleAt().
    std::unique_ptr<LoadedModuleSymbols> symbols;
  };

  // Creates the ModuleInfo structure, attempts to load the symbols, and
  // updates the modules_ list for this process. *err will be filled with the
  // success code of symbol loading (the function will save the ModuleInfo
  // either way).
  //
  // This class issues no notifications, the caller needs to do that. Just
  // because there's no error doesn't necessarily mean the symbols have been
  // loaded, since some symbols might be expected to be not present.
  ModuleInfo* SaveModuleInfo(const debug_ipc::Module& module,
                             Err* symbol_load_err);

  // Equality comparison for the two types of modules. This compares load
  // address and build id.
  static bool RefersToSameModule(const debug_ipc::Module& a,
                                 const ModuleInfo& b);

  // Looks up the given address and returns the module it's part of. Returns
  // null if the address is out-of-range.
  const ModuleInfo* InfoForAddress(uint64_t address) const;

  Notifications* const notifications_;   // Non-owning.
  TargetSymbols* const target_symbols_;  // Non-owning.

  // Maps load address to the module symbol information.
  using ModuleMap = std::map<uint64_t, ModuleInfo>;
  ModuleMap modules_;

  // Mutable so we can get weak pointers to a const object.
  mutable fxl::WeakPtrFactory<const ProcessSymbols> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessSymbols);
};

}  // namespace zxdb
