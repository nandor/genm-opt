// This file if part of the llir-opt project.
// Licensing information can be found in the LICENSE file.
// (C) 2018 Nandor Licker. All rights reserved.

#include <set>
#include <sstream>

#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Object/Archive.h>

#include "core/bitcode.h"
#include "core/printer.h"
#include "core/prog.h"
#include "core/util.h"

#include "driver.h"
#include "options.h"
#include "linker.h"



// -----------------------------------------------------------------------------
llvm::Error MakeError(llvm::Twine msg)
{
  return llvm::make_error<llvm::StringError>(msg, llvm::inconvertibleErrorCode());
}

// -----------------------------------------------------------------------------
llvm::Error WithTemp(
    llvm::StringRef ext,
    std::function<llvm::Error(int, llvm::StringRef)> &&f)
{
  // Write the program to a bitcode file.
  auto tmpOrError = llvm::sys::fs::TempFile::create("/tmp/llir-ld-%%%%%%%" + ext);
  if (!tmpOrError) {
    return tmpOrError.takeError();
  }
  auto &tmp = tmpOrError.get();

  // Run the program on the temp file, which is kept on failure.
  auto status = f(tmp.FD, tmp.TmpName);
  if (auto error = status ? tmp.keep() : tmp.discard()) {
    return error;
  }
  return status;
}

// -----------------------------------------------------------------------------
static OptLevel ParseOptLevel(llvm::opt::Arg *arg)
{
  if (arg) {
    switch (arg->getOption().getID()) {
      case OPT_O0: return OptLevel::O0;
      case OPT_O1: return OptLevel::O1;
      case OPT_O2: return OptLevel::O2;
      case OPT_O3: return OptLevel::O3;
      case OPT_O4: return OptLevel::O4;
      case OPT_Os: return OptLevel::Os;
      default: llvm_unreachable("invalid optimisation level");
    }
  } else {
    return OptLevel::O2;
  }
}

// -----------------------------------------------------------------------------
Driver::Driver(
    const llvm::Triple &triple,
    const llvm::Triple &base,
    llvm::opt::InputArgList &args)
  : llirTriple_(triple)
  , baseTriple_(base)
  , args_(args)
  , output_(Abspath(args.getLastArgValue(OPT_o, "a.out")))
  , shared_(args.hasArg(OPT_shared))
  , static_(args.hasArg(OPT_static))
  , relocatable_(args.hasArg(OPT_relocatable))
  , exportDynamic_(args.hasArg(OPT_export_dynamic))
  , targetCPU_(args.getLastArgValue(OPT_mcpu))
  , targetABI_(args.getLastArgValue(OPT_mabi))
  , targetFS_(args.getLastArgValue(OPT_mfs))
  , entry_(args.getLastArgValue(OPT_entry, "_start"))
  , rpath_(args.getLastArgValue(OPT_rpath))
  , optLevel_(ParseOptLevel(args.getLastArg(OPT_O_Group)))
  , libraryPaths_(args.getAllArgValues(OPT_library_path))
{
}

// -----------------------------------------------------------------------------
llvm::Expected<std::vector<std::unique_ptr<Prog>>>
Driver::LoadArchive(llvm::MemoryBufferRef buffer)
{
  // Parse the archive.
  auto libOrErr = llvm::object::Archive::create(buffer);
  if (!libOrErr) {
    return libOrErr.takeError();
  }

  // Decode all LLIR objects, dump the rest to text files.
  llvm::Error err = llvm::Error::success();
  std::vector<std::unique_ptr<Prog>> progs;
  for (auto &child : libOrErr.get()->children(err)) {
    auto bufferOrErr = child.getBuffer();
    if (!bufferOrErr) {
      return std::move(bufferOrErr.takeError());
    }
    auto buffer = bufferOrErr.get();
    if (IsLLIRObject(buffer)) {
      auto prog = BitcodeReader(buffer).Read();
      if (!prog) {
        return MakeError("cannot parse bitcode");
      }
      progs.emplace_back(std::move(prog));
    } else {
      llvm_unreachable("not implemented");
    }
  }
  if (err) {
    return std::move(err);
  } else {
    return progs;
  }
}

// -----------------------------------------------------------------------------
llvm::Expected<std::optional<std::vector<std::unique_ptr<Prog>>>>
Driver::TryLoadArchive(const std::string &path)
{
  if (llvm::sys::fs::exists(path)) {
    // Open the file.
    auto fileOrErr = llvm::MemoryBuffer::getFile(path);
    if (auto ec = fileOrErr.getError()) {
      return llvm::errorCodeToError(ec);
    }

    // Load the archive.
    auto buffer = fileOrErr.get()->getMemBufferRef();
    auto modulesOrErr = LoadArchive(buffer);
    if (!modulesOrErr) {
      return modulesOrErr.takeError();
    }

    // Record the files.
    std::vector<std::unique_ptr<Prog>> objects;
    for (auto &&module : *modulesOrErr) {
      objects.push_back(std::move(module));
    }
    return std::move(objects);
  }
  return std::nullopt;
};

// -----------------------------------------------------------------------------
llvm::Error Driver::Link()
{
  // Determine the entry symbols.
  std::set<std::string> entries;
  if (!shared_ && !relocatable_) {
    entries.insert(entry_);
  }

  // Collect objects and archives.
  std::vector<std::unique_ptr<Prog>> objects;
  std::vector<std::unique_ptr<Prog>> archives;
  bool wholeArchive = false;
  for (auto *arg : args_) {
    if (arg->isClaimed()) {
      continue;
    }

    switch (arg->getOption().getID()) {
      case OPT_INPUT: {
        llvm::StringRef path = arg->getValue();
        std::string fullPath = Abspath(path);

        // Open the file.
        auto FileOrErr = llvm::MemoryBuffer::getFile(fullPath);
        if (auto ec = FileOrErr.getError()) {
          return llvm::errorCodeToError(ec);
        }

        auto memBuffer = FileOrErr.get()->getMemBufferRef();
        auto buffer = memBuffer.getBuffer();
        if (IsLLIRObject(buffer)) {
          // Decode an object.
          auto prog = Parse(buffer, fullPath);
          if (!prog) {
            return MakeError("cannot read object: " + fullPath);
          }
          objects.push_back(std::move(prog));
          continue;
        }
        if (buffer.startswith("!<arch>")) {
          // Decode an archive.
          auto modulesOrErr = LoadArchive(memBuffer);
          if (!modulesOrErr) {
            return modulesOrErr.takeError();
          }
          for (auto &&module : modulesOrErr.get()) {
            if (wholeArchive) {
              objects.emplace_back(std::move(module));
            } else {
              archives.emplace_back(std::move(module));
            }
          }
          continue;
        }
        // Forward the input to the linker.
        externLibs_.push_back(path);
        continue;
      }
      case OPT_library: {
        bool found = false;
        llvm::StringRef name = arg->getValue();
        for (const std::string &libPath : libraryPaths_) {
          llvm::SmallString<128> path(libPath);
          if (name.startswith(":")) {
            llvm::sys::path::append(path, name.substr(1));
            auto fullPath = Abspath(std::string(path));
            if (llvm::StringRef(fullPath).endswith(".a")) {
              auto archiveOrError = TryLoadArchive(fullPath);
              if (!archiveOrError) {
                return archiveOrError.takeError();
              }
              if (auto &archive = archiveOrError.get()) {
                for (auto &&module : *archive) {
                  if (wholeArchive) {
                    objects.emplace_back(std::move(module));
                  } else {
                    archives.emplace_back(std::move(module));
                  }
                }
                found = true;
                continue;
              }
            }
            if (llvm::sys::fs::exists(fullPath)) {
              // Shared libraries are always in executable form,
              // add them to the list of extern libraries.
              externLibs_.push_back(("-l" + name).str());
              found = true;
              break;
            }
          } else {
            llvm::sys::path::append(path, "lib" + name);
            auto fullPath = Abspath(std::string(path));

            if (!static_) {
              std::string pathSO = fullPath + ".so";
              if (llvm::sys::fs::exists(pathSO)) {
                // Shared libraries are always in executable form,
                // add them to the list of extern libraries.
                externLibs_.push_back(("-l" + name).str());
                found = true;
                break;
              }
            }

            auto archiveOrError = TryLoadArchive(fullPath + ".a");
            if (!archiveOrError) {
              return archiveOrError.takeError();
            }
            if (auto &archive = archiveOrError.get()) {
              for (auto &&module : *archive) {
                if (wholeArchive) {
                  objects.emplace_back(std::move(module));
                } else {
                  archives.emplace_back(std::move(module));
                }
              }
              found = true;
              continue;
            }
          }
        }

        if (!found) {
          return MakeError("cannot find library " + name);
        }
        continue;
      }
      case OPT_whole_archive: {
        wholeArchive = true;
        continue;
      }
      case OPT_no_whole_archive: {
        wholeArchive = false;
        continue;
      }
      default: {
        arg->render(args_, forwarded_);
        continue;
      }
    }
  }

  // Link the objects together.
  auto prog = Linker(std::move(objects), std::move(archives), output_).Link();
  if (!prog) {
    return MakeError("linking failed");
  }
  return Output(GetOutputType(), *prog);
}

// -----------------------------------------------------------------------------
Driver::OutputType Driver::GetOutputType()
{
  llvm::StringRef o(output_);
  if (relocatable_) {
    return OutputType::LLBC;
  } else if (o.endswith(".S") || o.endswith(".s")) {
    return OutputType::ASM;
  } else if (o.endswith(".o")) {
    return OutputType::OBJ;
  } else if (o.endswith(".llir")) {
    return OutputType::LLIR;
  } else if (o.endswith(".llbc")) {
    return OutputType::LLBC;
  } else {
    return OutputType::EXE;
  }
}

// -----------------------------------------------------------------------------
llvm::Error Driver::Output(OutputType type, Prog &prog)
{
  switch (type) {
    case OutputType::LLIR: {
      // Write the llir output.
      std::error_code err;
      auto output = std::make_unique<llvm::ToolOutputFile>(
          output_,
          err,
          llvm::sys::fs::F_None
      );
      if (err) {
        return llvm::errorCodeToError(err);
      }

      Printer(output->os()).Print(prog);
      output->keep();
      return llvm::Error::success();
    }
    case OutputType::LLBC: {
      // Write the llbc output.
      std::error_code err;
      auto output = std::make_unique<llvm::ToolOutputFile>(
          output_,
          err,
          llvm::sys::fs::F_None
      );
      if (err) {
        return llvm::errorCodeToError(err);
      }

      BitcodeWriter(output->os()).Write(prog);
      output->keep();
      return llvm::Error::success();
    }
    case OutputType::EXE:
    case OutputType::OBJ:
    case OutputType::ASM: {
      // Lower the final program to the desired format.
      return WithTemp(".llbc", [&](int fd, llvm::StringRef llirPath) {
        {
          llvm::raw_fd_ostream os(fd, false);
          BitcodeWriter(os).Write(prog);
        }

        if (type != OutputType::EXE) {
          return RunOpt(llirPath, output_, type);
        } else {
          return WithTemp(".o", [&](int, llvm::StringRef elfPath) {
            auto error = RunOpt(llirPath, elfPath, OutputType::OBJ);
            if (error) {
              return error;
            }

            const std::string ld = baseTriple_.str() + "-ld";
            std::vector<llvm::StringRef> args;
            args.push_back(ld);

            // Architecture-specific flags.
            switch (baseTriple_.getArch()) {
              case llvm::Triple::x86_64:
              case llvm::Triple::llir_x86_64: {
                args.push_back("--no-ld-generated-unwind-info");
                break;
              }
              case llvm::Triple::aarch64:
              case llvm::Triple::llir_aarch64: {
                break;
              }
              case llvm::Triple::riscv64:
              case llvm::Triple::llir_riscv64: {
                break;
              }
              case llvm::Triple::ppc64le:
              case llvm::Triple::llir_ppc64le: {
                break;
              }
              default: {
                return MakeError("unknown target: " + baseTriple_.str());
              }
            }
            // Common flags.
            args.push_back("-nostdlib");
            // Output file.
            args.push_back("-o");
            args.push_back(output_);
            // Entry point.
            args.push_back("-e");
            args.push_back(entry_);
            // rpath.
            if (!rpath_.empty()) {
              args.push_back("-rpath");
              args.push_back(rpath_);
            }
            // Forwarded arguments.
            for (const auto &forwarded : forwarded_) {
              args.push_back(forwarded);
            }
            // Link the inputs.
            args.push_back("--start-group");
            // LLIR-to-ELF code.
            args.push_back(elfPath);
            // Library paths.
            if (!externLibs_.empty()) {
              for (llvm::StringRef lib : libraryPaths_) {
                args.push_back("-L");
                args.push_back(lib);
              }
            }
            // External libraries.
            for (llvm::StringRef lib : externLibs_) {
              args.push_back(lib);
            }
            // Extern objects.
            for (const auto &[tmp, path] : externFiles_) {
              args.push_back(path);
            }
            args.push_back("--end-group");
            if (shared_) {
              // Shared library options.
              args.push_back("-shared");
            } else {
              // Executable options.
              if (static_) {
                // Static executable options.
                args.push_back("-static");
              } else {
                // Dynamic executable options.
                if (exportDynamic_) {
                  args.push_back("-E");
                }
              }
            }
            // Run the linker.
            return RunExecutable(ld, args);
          });
        }
      });
    }
  }
  llvm_unreachable("invalid output type");
}

// -----------------------------------------------------------------------------
llvm::Error Driver::RunOpt(
    llvm::StringRef input,
    llvm::StringRef output,
    OutputType type)
{
  std::string toolName = llirTriple_.str() + "-opt";
  std::vector<llvm::StringRef> args;
  args.push_back(toolName);
  if (auto *opt = getenv("LLIR_OPT_O")) {
    args.push_back(opt);
  } else {
    switch (optLevel_) {
      case OptLevel::O0: args.push_back("-O0"); break;
      case OptLevel::O1: args.push_back("-O1"); break;
      case OptLevel::O2: args.push_back("-O2"); break;
      case OptLevel::O3: args.push_back("-O3"); break;
      case OptLevel::O4: args.push_back("-O4"); break;
      case OptLevel::Os: args.push_back("-Os"); break;
    }
  }
  // -mcpu
  if (auto *cpu = getenv("LLIR_OPT_CPU")) {
    args.push_back("-mcpu");
    args.push_back(cpu);
  } else if (!targetCPU_.empty()) {
    args.push_back("-mcpu");
    args.push_back(targetCPU_);
  }
  // -mabi
  if (auto *abi = getenv("LLIR_OPT_ABI")) {
    args.push_back("-mabi");
    args.push_back(abi);
  } else if (!targetABI_.empty()) {
    args.push_back("-mabi");
    args.push_back(targetABI_);
  }
  // -mfs
  if (auto *abi = getenv("LLIR_OPT_FS")) {
    args.push_back("-mfs");
    args.push_back(abi);
  } else if (!targetFS_.empty()) {
    args.push_back("-mfs");
    args.push_back(targetFS_);
  }
  // Additional flags.
  if (auto *flags = getenv("LLIR_OPT_FLAGS")) {
    llvm::SmallVector<llvm::StringRef, 3> tokens;
    llvm::StringRef(flags).split(tokens, " ", -1, false);
    for (llvm::StringRef flag : tokens) {
      args.push_back(flag);
    }
  }
  args.push_back("-o");
  args.push_back(output);
  args.push_back(input);
  if (shared_) {
    args.push_back("-shared");
  }
  if (static_) {
    args.push_back("-static");
  }
  if (!entry_.empty()) {
    args.push_back("-entry");
    args.push_back(entry_);
  }
  args.push_back("-emit");
  switch (type) {
    case OutputType::EXE: args.push_back("obj"); break;
    case OutputType::OBJ: args.push_back("obj"); break;
    case OutputType::ASM: args.push_back("asm"); break;
    case OutputType::LLIR: args.push_back("llir"); break;
    case OutputType::LLBC: args.push_back("llbc"); break;
  }
  return RunExecutable(toolName, args);
}

// -----------------------------------------------------------------------------
llvm::Error Driver::RunExecutable(
    llvm::StringRef exe,
    llvm::ArrayRef<llvm::StringRef> args)
{
  if (auto P = llvm::sys::findProgramByName(exe)) {
    if (auto code = llvm::sys::ExecuteAndWait(*P, args)) {
      std::string str;
      llvm::raw_string_ostream os(str);
      os << "command failed: " << exe << " ";
      for (size_t i = 1, n = args.size(); i < n; ++i) {
        os << args[i] << " ";
      }
      os << "\n";
      return MakeError(str);
    }
    return llvm::Error::success();
  }
  return MakeError("missing executable " + exe);
}
