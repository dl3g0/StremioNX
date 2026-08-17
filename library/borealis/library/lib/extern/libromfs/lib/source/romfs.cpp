#include <romfs/romfs.hpp>

#include <map>
#include <stdexcept>

const std::map<std::string, romfs::Resource>& ROMFS_CONCAT(ROMFS_NAME, _get_resources)();
const std::vector<std::string>& ROMFS_CONCAT(ROMFS_NAME, _get_paths)();
const std::string& ROMFS_CONCAT(ROMFS_NAME, _get_name)();

namespace romfs {

    const romfs::Resource &impl::ROMFS_CONCAT(get_, LIBROMFS_PROJECT_NAME)(const std::string &path) {
        try {
            return ROMFS_CONCAT(ROMFS_NAME, _get_resources)().at(path);
        } catch (std::out_of_range &ignored) {
            throw std::invalid_argument(std::string("Invalid romfs resource path for '" + romfs::name() + "' : ") + path);
        }
    }

    std::vector<std::string> impl::ROMFS_CONCAT(list_, LIBROMFS_PROJECT_NAME)(const std::string &parent) {
        printf("DEBUG: romfs::list C-function called with parent='%s'\n", parent.c_str());
        fflush(stdout);
        if (parent.empty()) {
            return ROMFS_CONCAT(ROMFS_NAME, _get_paths)();
        } else {
            std::vector<std::string> result;
            for (const auto &p : ROMFS_CONCAT(ROMFS_NAME, _get_paths)()) {
                // Check if 'p' is inside 'parent' directory
                // p starts with parent
                if (p.rfind(parent, 0) == 0) {
                    // Check if it is exactly the parent path or a file inside it
                    std::string remainder = p.substr(parent.length());
                    if (!remainder.empty() && remainder[0] == '/') {
                        // ensure there are no more '/' in the remainder, so it's a direct child
                        if (remainder.find('/', 1) == std::string::npos) {
                            result.push_back(p);
                        }
                    } else if (parent.back() == '/' && remainder.find('/') == std::string::npos) {
                        result.push_back(p);
                    }
                }
            }
            printf("DEBUG: romfs::list C-function returning %zu results\n", result.size());
            return result;
        }
    }

    const std::string &impl::ROMFS_CONCAT(name_, LIBROMFS_PROJECT_NAME)() {
        return ROMFS_CONCAT(ROMFS_NAME, _get_name)();
    }

}