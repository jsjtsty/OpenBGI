#pragma once
#include <filesystem>
#include <map>

#include "vfs/arc.hpp"
#include "vm/vm.hpp"
#include "resources/archive_store.hpp"

namespace obgi {

// Serves _bp program blocks straight from the game's ARC20 archives (decoding DSC members).
class ArcProgramProvider final : public vm::ProgramProvider {
  public:
    explicit ArcProgramProvider(std::filesystem::path game_dir);
    explicit ArcProgramProvider(ArcResourceFinder& resources) : resources_(&resources) {}
    Result<std::vector<std::uint8_t>> load(std::string_view arc, std::string_view name) override;

  private:
    std::unique_ptr<ArcResourceFinder> owned_;
    ArcResourceFinder* resources_;

};

} // namespace obgi
