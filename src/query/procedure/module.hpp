/// @file
/// API for loading and registering modules providing custom oC procedures
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "query/procedure/mg_procedure_impl.hpp"
#include "utils/memory.hpp"
#include "utils/rw_lock.hpp"

namespace query::procedure {

class Module {
 public:
  Module() {}
  virtual ~Module();
  Module(const Module &) = delete;
  Module(Module &&) = delete;
  Module &operator=(const Module &) = delete;
  Module &operator=(Module &&) = delete;

  /// Invokes the (optional) shutdown function and closes the module.
  virtual bool Close() = 0;

  /// Reloads the module.
  virtual bool Reload() = 0;

  /// Returns registered procedures of this module
  virtual const std::map<std::string, mgp_proc, std::less<>> *Procedures()
      const = 0;
};

/// Proxy for a registered Module, acquires a read lock from ModuleRegistry.
class ModulePtr final {
  const Module *module_{nullptr};
  std::shared_lock<utils::RWLock> lock_;

 public:
  ModulePtr() = default;
  ModulePtr(std::nullptr_t) {}
  ModulePtr(const Module *module, std::shared_lock<utils::RWLock> lock)
      : module_(module), lock_(std::move(lock)) {}

  explicit operator bool() const { return static_cast<bool>(module_); }

  const Module &operator*() const { return *module_; }
  const Module *operator->() const { return module_; }
};

/// Thread-safe registration of modules from libraries, uses utils::RWLock.
class ModuleRegistry final {
  std::map<std::string, std::unique_ptr<Module>, std::less<>> modules_;
  mutable utils::RWLock lock_{utils::RWLock::Priority::WRITE};

 public:
  ModuleRegistry();

  /// Load a module from the given path and return true if successful.
  ///
  /// A write lock is taken during the execution of this method. Loading a
  /// module is done through `dlopen` facility and path is resolved accordingly.
  /// The module is registered using the filename part of the path, with the
  /// extension removed. If a module with the same name already exists, the
  /// function does nothing.
  bool LoadModuleLibrary(std::filesystem::path path);

  /// Find a module with given name or return nullptr.
  /// Takes a read lock.
  ModulePtr GetModuleNamed(const std::string_view &name) const;

  /// Reload a module with given name and return true if successful.
  /// Takes a write lock. Builtin modules cannot be reloaded, though true will
  /// be returned if you try to do so. If false was returned, then the module is
  /// no longer registered.
  bool ReloadModuleNamed(const std::string_view &name);

  /// Reload all loaded (non-builtin) modules and return true if successful.
  /// Takes a write lock. If false was returned, the module which failed to
  /// reload is no longer registered. Remaining modules may or may not be
  /// reloaded, but are valid and registered.
  bool ReloadAllModules();

  /// Remove all loaded (non-builtin) modules.
  /// Takes a write lock.
  void UnloadAllModules();
};

/// Single, global module registry.
extern ModuleRegistry gModuleRegistry;

/// Return the ModulePtr and `mgp_proc *` of the found procedure after resolving
/// `fully_qualified_procedure_name`. `memory` is used for temporary allocations
/// inside this function. ModulePtr must be kept alive to make sure it won't be
/// unloaded.
std::optional<std::pair<procedure::ModulePtr, const mgp_proc *>> FindProcedure(
    const ModuleRegistry &module_registry,
    const std::string_view &fully_qualified_procedure_name,
    utils::MemoryResource *memory);

}  // namespace query::procedure
