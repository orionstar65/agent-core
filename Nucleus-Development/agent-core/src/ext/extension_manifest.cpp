#include "agent/extension_manager.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace agent {

std::vector<ExtensionSpec> load_extension_manifest(const std::string& manifest_path) {
    std::vector<ExtensionSpec> specs;
    
    try {
        std::ifstream file(manifest_path);
        if (!file) {
            std::cerr << "ExtensionManifest: Failed to open manifest: " << manifest_path << "\n";
            return specs;
        }
        
        json j;
        file >> j;
        
        if (!j.contains("extensions") || !j["extensions"].is_array()) {
            std::cerr << "ExtensionManifest: Invalid manifest format\n";
            return specs;
        }
        
        for (const auto& ext : j["extensions"]) {
            ExtensionSpec spec;
            spec.name = ext.value("name", "");
            spec.exec_path = ext.value("execPath", "");
            spec.critical = ext.value("critical", true);
            spec.enabled = ext.value("enabled", true);
            
            if (ext.contains("args") && ext["args"].is_array()) {
                for (const auto& arg : ext["args"]) {
                    spec.args.push_back(arg.get<std::string>());
                }
            }
            
            if (!spec.name.empty() && !spec.exec_path.empty()) {
                specs.push_back(spec);
            }
        }
        
        std::cout << "ExtensionManifest: Loaded " << specs.size() << " extension(s)\n";
        
    } catch (const std::exception& e) {
        std::cerr << "ExtensionManifest: Failed to parse manifest: " << e.what() << "\n";
    }
    
    return specs;
}

}
