// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#pragma once

#include <string>
#include <optional>
#include <string_view>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Alignment.h>

#include "core/user.h"
#include "core/visibility.h"

class Prog;



/**
 * Base for global symbols.
 */
class Global : public User {
public:
  /// Kind of the global.
  static constexpr Value::Kind kValueKind = Value::Kind::GLOBAL;

public:
  /// Enumeration of global kinds.
  enum class Kind {
    EXTERN,
    FUNC,
    BLOCK,
    ATOM
  };

public:
  Global(
      Kind kind,
      const std::string_view name,
      Visibility visibility = Visibility::LOCAL,
      unsigned numOps = 0
  );
  virtual ~Global();

  /// Returns the kind of the global.
  Kind GetKind() const { return kind_; }
  /// Checks if the global is of a specific kind.
  bool Is(Kind kind) const { return GetKind() == kind; }

  /// Returns the name of the global.
  const std::string_view GetName() const { return name_; }
  /// Returns the name of the basic block for LLVM.
  llvm::StringRef getName() const { return name_; }

  /// Externs have no known alignment.
  virtual std::optional<llvm::Align> GetAlignment() const = 0;

  /// Sets the visibilty of the global.
  void SetVisibility(Visibility visibility) { visibility_ = visibility; }
  /// Returns the visibilty of a global.
  Visibility GetVisibility() const { return visibility_; }

  /// Checks if the symbol can be externally referenced.
  bool IsRoot() const;
  /// Checks if the global is hidden in the compilation unit.
  bool IsLocal() const;
  /// Checks if a symbol is weak
  bool IsWeak() const;

  /// Removes the global from the parent container.
  virtual void removeFromParent() = 0;
  /// Removes the global from the parent container, deleting it.
  virtual void eraseFromParent() = 0;

  /// Returns the program to which the global belongs.
  virtual Prog *getProg() = 0;

private:
  friend class Prog;
  /// Kind of the global.
  Kind kind_;
  /// Name of the global.
  std::string name_;
  /// Visibility of the global.
  Visibility visibility_;
};
