#pragma once
#include <filesystem>
#include <map>

#include "vfs/arc.hpp"
#include "vm/vm.hpp"

namespace obgi {

// Serves _bp program blocks straight from the game's ARC20 archives (decoding DSC members).
class ArcProgramProvider final : public vm::ProgramProvider {
  public:
    explicit ArcProgramProvider(std::filesystem::path game_dir);
    Result<std::vector<std::uint8_t>> load(std::string_view arc, std::string_view name) override;

  private:
    Result<Arc*> open(std::string_view arc);

    std::filesystem::path dir_;
    std::map<std::string, std::filesystem::path> files_; // lowercase file name -> path
    std::map<std::string, Arc> arcs_;
};

} // namespace obgi
