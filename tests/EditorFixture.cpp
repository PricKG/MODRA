#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string mode = argv[1];
    if (mode == "--fail") return 7;
    if (mode == "--verify-template") {
        std::ifstream input(argv[2], std::ios::binary);
        const std::string document{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (document.find("MODRA_DOCUMENT_V1") == std::string::npos ||
            document.find("# Nota existente") == std::string::npos ||
            document.find("Cuerpo existente") == std::string::npos) {
            return 6;
        }
        input.close();
    } else if (mode != "--write" && mode != "--invalid") {
        return 3;
    }
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    if (!output) return 4;
    if (mode == "--invalid") {
        output << "## Documento sin título principal\n\nContenido recuperable\n";
    } else if (mode == "--verify-template") {
        output << "# Título cambiado desde Markdown\n\nCuerpo cambiado desde Markdown.\n";
    } else {
        output << "# Nota editada\n\n## Contexto\n\nLínea UTF-8: solución técnica\n\n"
                  "- elemento uno\n- elemento dos\n\n"
                  "| Clave | Valor |\n| --- | --- |\n| A | B |\n\n"
                  "```sql\nSELECT 1;\n```\n";
    }
    return output ? 0 : 5;
}
