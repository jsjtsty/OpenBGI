#include "bindings/arc_program_provider.hpp"
namespace obgi {
ArcProgramProvider::ArcProgramProvider(std::filesystem::path dir)
    : owned_(std::make_unique<ArcResourceFinder>(std::move(dir))), resources_(owned_.get()) {}
Result<std::vector<std::uint8_t>> ArcProgramProvider::load(std::string_view arc, std::string_view name) {
    return resources_->read_archive(arc, name);
}
} // namespace obgi
