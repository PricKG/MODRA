#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "application/NoteDocument.h"

TEST_CASE("A MODRA note document combines help title and body") {
    const std::string body = "## Contexto\n\nContenido técnico.";
    const std::string document = modra::build_note_document("Documento técnico", body);

    CHECK(document.find("MODRA_DOCUMENT_V1") != std::string::npos);
    CHECK(document.find("# Documento técnico\n\n") != std::string::npos);
    const auto parsed = modra::parse_note_document(document);
    CHECK(parsed.title == "Documento técnico");
    CHECK(parsed.body == body);
}

TEST_CASE("The controlled parser accepts leading space LF CRLF and UTF-8 BOM") {
    const auto lf = modra::parse_note_document("  \n\n<!--\nMODRA_DOCUMENT_V1\n-->\n\n# Título LF\n\nCuerpo LF");
    CHECK(lf.title == "Título LF");
    CHECK(lf.body == "Cuerpo LF");

    const auto crlf = modra::parse_note_document(
        "\xEF\xBB\xBF  \r\n<!--\r\nMODRA_DOCUMENT_V1\r\n-->\r\n\r\n# Título CRLF\r\n\r\nLínea 1\r\nLínea 2");
    CHECK(crlf.title == "Título CRLF");
    CHECK(crlf.body == "Línea 1\r\nLínea 2");
}

TEST_CASE("The controlled parser validates main title and required body") {
    CHECK_THROWS_WITH(modra::parse_note_document("## Subtítulo\n\nContenido"),
                      "El documento debe contener un título con el formato: # Título");
    CHECK_THROWS_WITH(modra::parse_note_document("#    \n\nContenido"),
                      "El documento debe contener un título con el formato: # Título");
    CHECK_THROWS_WITH(modra::parse_note_document("<!-- MODRA_DOCUMENT_V1 -->\n\n# Título\n\n  \r\n\t"),
                      "El cuerpo del documento no puede estar vacío.");
}

TEST_CASE("A heading inside a fenced block is not the document title") {
    const std::string document =
        "```markdown\n# Título falso\n```\n\n## Tampoco es título\n\n# Título real\n\nCuerpo real";
    const auto parsed = modra::parse_note_document(document);
    CHECK(parsed.title == "Título real");
    CHECK(parsed.body == "Cuerpo real");
}

TEST_CASE("Rich Markdown body is preserved without interpreting it") {
    const std::string body = R"DOC(## Contexto

- Uno
- Dos

| Columna | Valor |
| --- | --- |
| A | B |

```cpp
# esto no es un título
int main() {}
```

> Una cita con [enlace](https://example.com).

<!-- comentario adicional conservado -->)DOC";
    const auto parsed = modra::parse_note_document(modra::build_note_document("  Título  con espacios internos  ", body));

    CHECK(parsed.title == "Título  con espacios internos");
    CHECK(parsed.body == body);
    CHECK(parsed.body.find("## Contexto") == 0);
    CHECK(parsed.body.find("<!-- comentario adicional conservado -->") != std::string::npos);
}
