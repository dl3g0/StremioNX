#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#if __cplusplus > 202002L
#include <span>
#endif

#include "nonstd/span.hpp"

#define ROMFS_CONCAT_IMPL(x, y) x##y
#define ROMFS_CONCAT(x, y) ROMFS_CONCAT_IMPL(x, y)

#define ROMFS_NAME ROMFS_CONCAT(RomFs_, LIBROMFS_PROJECT_NAME)

namespace romfs {

    class Resource {
    public:
        constexpr Resource() : m_content() {}
        explicit constexpr Resource(const nonstd::span<std::byte> &content) : m_content(content) {}

        [[nodiscard]]
        constexpr const std::byte* data() const {
            return this->m_content.data();
        }

        [[nodiscard]]
        constexpr std::size_t size() const {
            return this->m_content.size();
        }

        [[nodiscard]]
        std::string_view string() const {
            return { reinterpret_cast<const char*>(this->data()), this->size() };
        }

//        [[nodiscard]]
//        std::u8string_view u8string() const {
//            return { reinterpret_cast<const char8_t *>(this->data()), this->size() + 1 };
//        }

        [[nodiscard]]
        constexpr bool valid() const {
            return !this->m_content.empty() && this->m_content.data() != nullptr;
        }

    private:
        const nonstd::span<const std::byte> m_content;
    };

    namespace impl {

        [[nodiscard]] const Resource& ROMFS_CONCAT(get_, LIBROMFS_PROJECT_NAME)(const std::string &path);
        [[nodiscard]] std::vector<std::string> ROMFS_CONCAT(list_, LIBROMFS_PROJECT_NAME)(const std::string &path);
        [[nodiscard]] const std::string& ROMFS_CONCAT(name_, LIBROMFS_PROJECT_NAME)();

    }

    [[nodiscard]] inline const Resource& get(const std::string &path) { return impl::ROMFS_CONCAT(get_, LIBROMFS_PROJECT_NAME)(path); }
    [[nodiscard]] inline std::vector<std::string> list(const std::string &path = {}) { return impl::ROMFS_CONCAT(list_, LIBROMFS_PROJECT_NAME)(path); }
    [[nodiscard]] inline const std::string& name() { return impl::ROMFS_CONCAT(name_, LIBROMFS_PROJECT_NAME)(); }


}