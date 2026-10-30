#include "parser.h"
#include "codegen.h"
#include "optimize.h"
#include "module_loader.h"

/* The production parser and JS backend, running only in a browser worker. */
int main(void) {
    FILE *input = fopen("/input.fg", "rb");
    if (!input) forge_die("missing playground input");
    char *source = malloc(65537);
    if (!source) forge_die("out of memory");
    size_t length = fread(source, 1, 65537, input);
    fclose(input);
    if (length > 65536) forge_die("source exceeds 64 KiB");
    source[length] = '\0';
    Lexer lexer;
    lexer_init(&lexer, source, length);
    Program program = parse_program(&lexer);
    ForgeModuleConfig modules = {.entry_path = "/input.fg"};
    forge_load_modules(&program, &modules);
    optimize_program(&program);
    FILE *output = fopen("/output.js", "wb");
    if (!output) forge_die("cannot open output");
    codegen_emit_js(&program, output);
    fclose(output);
    program_free(&program);
    free(source);
    return 0;
}
