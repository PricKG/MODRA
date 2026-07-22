#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string mode = argv[1];
    if (mode == "--fail") return 7;
    if (mode != "--write") return 3;
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) return 4;
    output << "# Nota editada\n\nLínea UTF-8: solución técnica\n\n```sql\nSELECT 1;\n```\n";
    return output ? 0 : 5;
}
