/*
    Copyright 2020-2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/core/i18n.hpp>
#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include("experimental/filesystem")
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "Failed to include <filesystem> header!"
#endif
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#ifndef BRLS_I18N_PREFIX
#define BRLS_I18N_PREFIX ""
#endif

namespace brls
{

static nlohmann::json defaultLocale = {};
static nlohmann::json currentLocale = {};

static void loadLocale(std::string locale, nlohmann::json* target)
{
    printf("DEBUG-RAW: loadLocale ENTER for '%s'\n", locale.c_str());
    fflush(stdout);
    
    if (locale.empty()) {
        printf("DEBUG-RAW: locale is empty, returning\n");
        fflush(stdout);
        return;
    }
    
    try {
#ifdef USE_LIBROMFS
        printf("DEBUG-RAW: USE_LIBROMFS is defined, about to call romfs::list\n");
        fflush(stdout);
        
        std::string listArg = "i18n/" + locale;
        printf("DEBUG-RAW: romfs::list arg = '%s'\n", listArg.c_str());
        fflush(stdout);
        
        auto localePath = romfs::list(listArg);
        printf("DEBUG-RAW: romfs::list returned %zu entries\n", localePath.size());
        fflush(stdout);
        
        if (localePath.empty())
        {
            printf("DEBUG-RAW: locale dir is empty, returning\n");
            fflush(stdout);
            Logger::error("Cannot load locale {}: directory i18n/{} doesn't exist", locale, locale);
            return;
        }
        for (auto& path : localePath)
        {
            printf("DEBUG-RAW: processing file '%s'\n", path.c_str());
            fflush(stdout);
            
            std::string name;
            size_t slashPos = path.find_last_of('/');
            if (slashPos != std::string::npos) {
                name = path.substr(slashPos + 1);
            } else {
                name = path;
            }

            if (!endsWith(name, ".json"))
                continue;

            try {
                (*target)[name.substr(0, name.length() - 5)] = nlohmann::json::parse(romfs::get(path).string());
                printf("DEBUG-RAW: successfully parsed '%s'\n", name.c_str());
                fflush(stdout);
            } catch (const std::exception& e) {
                printf("DEBUG-RAW: EXCEPTION parsing '%s': %s\n", path.c_str(), e.what());
                fflush(stdout);
                Logger::error("Failed to parse locale json {}: {}", path, e.what());
            }
        }
#else
        printf("DEBUG-RAW: USE_LIBROMFS is NOT defined, using filesystem\n");
        fflush(stdout);
        
        std::string localePath = BRLS_ASSET("i18n/" + locale);
        printf("DEBUG-RAW: localePath = '%s'\n", localePath.c_str());
        fflush(stdout);

        if (!fs::exists(localePath))
        {
            printf("DEBUG-RAW: path does not exist\n");
            fflush(stdout);
            Logger::error("Cannot load locale {}: directory {} doesn't exist", locale, localePath);
            return;
        }
        else if (!fs::is_directory(localePath))
        {
            Logger::error("Cannot load locale {}: {} isn't a directory", locale, localePath);
            return;
        }

        // Iterate over all JSON files in the directory
        for (const fs::directory_entry& entry : fs::directory_iterator(localePath))
        {
            if (fs::is_directory(entry))
                continue;

            std::string name = entry.path().filename().string();

            if (!endsWith(name, ".json"))
                continue;

            std::string path = entry.path().string();

            nlohmann::json strings;

            std::ifstream jsonStream;
            jsonStream.open(path);

            try
            {
                jsonStream >> strings;
            }
            catch (const std::exception& e)
            {
                Logger::error("Error while loading \"{}\": {}", path, e.what());
            }

            jsonStream.close();

            (*target)[name.substr(0, name.length() - 5)] = strings;
        }
#endif /* USE_LIBROMFS */
    } catch (const std::exception& e) {
        printf("DEBUG-RAW: FATAL EXCEPTION in loadLocale: %s\n", e.what());
        fflush(stdout);
    } catch (...) {
        printf("DEBUG-RAW: UNKNOWN FATAL EXCEPTION in loadLocale\n");
        fflush(stdout);
    }
    
    printf("DEBUG-RAW: loadLocale EXIT\n");
    fflush(stdout);
}


void loadTranslations()
{
    Logger::info("DEBUG: Calling loadLocale for default");
    loadLocale(LOCALE_DEFAULT, &defaultLocale);

    Logger::info("DEBUG: Getting current locale");
    std::string currentLocaleName = Application::getLocale();
    Logger::info("DEBUG: current locale is {}", currentLocaleName);
    if (currentLocaleName != LOCALE_DEFAULT)
        loadLocale(currentLocaleName, &currentLocale);
}

namespace internal
{
    std::string getRawStr(std::string stringName)
    {
        nlohmann::json::json_pointer pointer;

        try
        {
            pointer = nlohmann::json::json_pointer("/" + std::string(BRLS_I18N_PREFIX) + stringName);
        }
        catch (const std::exception& e)
        {
            Logger::error("Error while getting string \"{}\": {}", stringName, e.what());
            return stringName;
        }

        // First look for translated string in current locale
        try
        {
            return currentLocale[pointer].get<std::string>();
        }
        catch (...)
        {
        }

        // Then look for default locale
        try
        {
            return defaultLocale[pointer].get<std::string>();
        }
        catch (...)
        {
        }

        // Fallback to returning the string name
        return stringName;
    }
} // namespace internal

inline namespace literals
{
    std::string operator"" _i18n(const char* str, size_t len)
    {
        return internal::getRawStr(std::string(str, len));
    }

} // namespace literals

} // namespace brls
