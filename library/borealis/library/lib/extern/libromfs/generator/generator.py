import sys
import os
import binascii

if len(sys.argv) != 3:
    print("Usage: generator.py <LIBROMFS_PROJECT_NAME> <LIBROMFS_RESOURCE_LOCATION>")
    sys.exit(1)

project_name = sys.argv[1]
resource_loc = sys.argv[2]

output_file = "libromfs_resources.cpp"

with open(output_file, 'w') as f:
    f.write("#include <romfs/romfs.hpp>\n\n")
    f.write("#include <array>\n")
    f.write("#include <map>\n\n\n")
    f.write("/* Resource definitions */\n")

    identifier_count = 0
    paths = []

    for root, dirs, files in os.walk(resource_loc):
        for file in files:
            if file == ".DS_Store":
                continue
            
            p = os.path.join(root, file)
            rel_path = os.path.relpath(p, resource_loc).replace('\\', '/')
            
            file_size = os.path.getsize(p)
            f.write(f"static std::array<std::uint8_t, {file_size + 1}> resource_{project_name}_{identifier_count} = {{\n    ")
            
            with open(p, 'rb') as in_f:
                content = in_f.read()
                
                # Write hex bytes
                hex_str = ", ".join([f"0x{b:02X}" for b in content])
                f.write(hex_str)
                if len(content) > 0:
                    f.write(", ")
                f.write("0x00 };\n\n")
                
            paths.append(rel_path)
            identifier_count += 1

    f.write("\n/* Resource map */\n")
    f.write(f"const std::map<std::string, romfs::Resource>& RomFs_{project_name}_get_resources() {{\n")
    f.write("    static std::map<std::string, romfs::Resource> resources = {\n")
    
    for i, path in enumerate(paths):
        path_escaped = path.replace('"', '\\"')
        f.write(f"        {{ \"{path_escaped}\", romfs::Resource({{ reinterpret_cast<std::byte*>(resource_{project_name}_{i}.data()), resource_{project_name}_{i}.size() - 1 }}) }},\n")
        
    f.write("    };\n    return resources;\n}\n\n")

    f.write("/* Resource paths */\n")
    f.write(f"const std::vector<std::string>& RomFs_{project_name}_get_paths() {{\n")
    f.write("    static std::vector<std::string> paths = {\n")
    
    for path in paths:
        path_escaped = path.replace('"', '\\"')
        f.write(f"        \"{path_escaped}\",\n")
        
    f.write("    };\n    return paths;\n}\n")
    
    f.write(f"\nconst std::string& RomFs_{project_name}_get_name() {{\n")
    f.write(f"    static std::string name = \"{project_name}\";\n")
    f.write("    return name;\n}\n")
