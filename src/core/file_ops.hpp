#ifndef BESTCOMPARE_FILE_OPS_HPP
#define BESTCOMPARE_FILE_OPS_HPP

#include <filesystem>
#include <system_error>
#include <string>

namespace BestCompare {

namespace fs = std::filesystem;

class FileOps {
public:
    static bool CopyPath(const fs::path& src, const fs::path& dst) {
        std::error_code ec;
        if (!fs::exists(src, ec)) return false;

        if (fs::is_directory(src, ec)) {
            fs::create_directories(dst, ec);
            for (const auto& entry : fs::recursive_directory_iterator(src, ec)) {
                const auto& path = entry.path();
                auto relative = fs::relative(path, src, ec);
                auto target = dst / relative;
                if (fs::is_directory(path, ec)) {
                    fs::create_directories(target, ec);
                } else {
                    fs::copy_file(path, target, fs::copy_options::overwrite_existing, ec);
                }
            }
            return !ec;
        } else {
            if (dst.has_parent_path()) {
                fs::create_directories(dst.parent_path(), ec);
            }
            return fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        }
    }

    static bool DeletePath(const fs::path& target) {
        std::error_code ec;
        if (!fs::exists(target, ec)) return true;
        if (fs::is_directory(target, ec)) {
            fs::remove_all(target, ec);
        } else {
            fs::remove(target, ec);
        }
        return !ec;
    }
};

} // namespace BestCompare

#endif // BESTCOMPARE_FILE_OPS_HPP
