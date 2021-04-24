#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

void parse_load_commands(FILE *fptr, int offset, uint32_t ncmds);
void parse_segments(FILE *fptr, struct segment_command_64 *seg_cmd);
void parse_cstring_section(FILE *fptr, struct section_64 *cstring_sect);
void parse_pointer_section(FILE *fptr, struct section_64 *sect);
void parse_symbol_table(FILE *fptr, struct symtab_command *sym_cmd);
void parse_dynamic_symbol_table(FILE *fptr, struct dysymtab_command *dysym_cmd);
void format_section_type(uint8_t type, char *out);
void format_n_desc(uint16_t n_desc, char *formatted);
void format_string(char *str, char *formatted);


void *load_bytes(FILE *fptr, int offset, int size) {
    void *buf = calloc(1, size);
    fseek(fptr, offset, SEEK_SET);
    fread(buf, size, 1, fptr);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("Error: missing Mach-O file.");
        return 1;
    }
    FILE *fptr = fopen(argv[1], "rb");

    struct mach_header_64 *header = load_bytes(fptr, 0, sizeof(struct mach_header_64));
    parse_load_commands(fptr, sizeof(struct mach_header_64), header->ncmds);

    free(header);
    fclose(fptr);
    return 0;
}

void parse_load_commands(FILE *fptr, int offset, uint32_t ncmds) {
    for (int i = 0; i < ncmds; ++i) {
        struct load_command *lcmd = load_bytes(fptr, offset, sizeof(struct load_command));

        if (lcmd->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *seg_cmd = load_bytes(fptr, offset, lcmd->cmdsize);
            parse_segments(fptr, seg_cmd);
            free(seg_cmd);
        } else if (lcmd->cmd == LC_SYMTAB) {
            struct symtab_command *cmd = load_bytes(fptr, offset, lcmd->cmdsize);
            parse_symbol_table(fptr, cmd);
            free(cmd);
        } else if (lcmd->cmd == LC_DYSYMTAB) {
            struct dysymtab_command *cmd = load_bytes(fptr, offset, lcmd->cmdsize);
            parse_dynamic_symbol_table(fptr, cmd);
            free(cmd);
        } else {
            // printf("Load command: %d\n", lcmd->cmd);
        }

        offset += lcmd->cmdsize;
        free(lcmd);
    }
}

void parse_segments(FILE *fptr, struct segment_command_64 *seg_cmd) {
    printf("LC_SEGMENT_64: %s (%lld)\n", seg_cmd->segname, seg_cmd->filesize);

    // section_64 is immediately after segment_command_64.
    struct section_64 *sections = (void *)seg_cmd + sizeof(struct segment_command_64);

    char formatted_type[32];
    char formatted_seg_sec[64];

    for (int i = 0; i < seg_cmd->nsects; ++i) {
        struct section_64 sect = sections[i];
        const uint8_t type = sect.flags & SECTION_TYPE;

        format_section_type(type, formatted_type);
        sprintf(formatted_seg_sec, "(%s,%s)", sect.segname, sect.sectname);
        printf("    %-32s [size: %4lld] [type: %-32s]\n", formatted_seg_sec, sect.size, formatted_type);

        // (__TEXT,__cstring), (__TEXT,__objc_classname__TEXT), (__TEXT,__objc_methname), etc..
        if (type == S_CSTRING_LITERALS) {
            parse_cstring_section(fptr, &sect);
        }
        // (__DATA_CONST,__mod_init_func)
        else if (type == S_MOD_INIT_FUNC_POINTERS
            || type == S_NON_LAZY_SYMBOL_POINTERS
            || type == S_LAZY_SYMBOL_POINTERS) {
            parse_pointer_section(fptr, &sect);
        }
    }
}

void parse_cstring_section(FILE *fptr, struct section_64 *cstring_sect) {
    void *section = load_bytes(fptr, cstring_sect->offset, cstring_sect->size);

    char formatted[256];
    for (char *ptr = section; ptr < (char *)(section + cstring_sect->size);) {
        if (strlen(ptr) > 0) {
            format_string(ptr, formatted);
            printf("        \"%s\"\n", formatted);
            ptr += strlen(ptr);
        }
        ptr += 1;
    }

    free(section);
}

void parse_pointer_section(FILE *fptr, struct section_64 *sect) {
    void *section = load_bytes(fptr, sect->offset, sect->size);

    const size_t count = sect->size / sizeof(uintptr_t);
    for (int i = 0; i < count; ++i) {
        printf("        0x%lx\n", *((uintptr_t *)section + i));
    }

    free(section);
}

void parse_symbol_table(FILE *fptr, struct symtab_command *sym_cmd) {
    printf("LC_SYMTAB (symtab: %lu, strtab: %u)\n", sym_cmd->nsyms * sizeof(struct nlist_64), sym_cmd->strsize);

    void *sym_table = load_bytes(fptr, sym_cmd->symoff, sym_cmd->nsyms * sizeof(struct nlist_64));
    void *str_table = load_bytes(fptr, sym_cmd->stroff, sym_cmd->strsize);

    char formatted_n_desc[256];

    for (int i = 0; i < sym_cmd->nsyms; ++i) {
        struct nlist_64 *nlist = sym_table + sizeof(struct nlist_64) * i;
        char *symbol = str_table + nlist->n_un.n_strx;

        format_n_desc(nlist->n_desc, formatted_n_desc);

        if (strlen(symbol) > 0) {
            printf("    %-3d 0x%016llx  %-32s", i, nlist->n_value, symbol);
            if (strlen(formatted_n_desc) > 0) {
                printf("  [n_desc:%s]\n", formatted_n_desc);
            } else {
                printf("\n");
            }
        }
    }

    free(sym_table);
    free(str_table);
}

void parse_dynamic_symbol_table(FILE *fptr, struct dysymtab_command *dysym_cmd) {
    printf("LC_DYSYMTAB\n");

    printf("    Indirect symbol table (indirectsymoff: 0x%x, nindirectsyms: %d)\n", dysym_cmd->indirectsymoff, dysym_cmd->nindirectsyms);
    uint32_t *indirect_symtab = (uint32_t *)load_bytes(fptr, dysym_cmd->indirectsymoff, dysym_cmd->nindirectsyms * sizeof(uint32_t)); // the index is 32 bits

    printf("        Indices: [");
    for (int i = 0; i < dysym_cmd->nindirectsyms; ++i) {
        printf("%d, ", *(indirect_symtab + i));
    }

    printf("]\n");
}

void format_section_type(uint8_t type, char *out) {
    if (type == S_REGULAR) {
        strcpy(out, "S_REGULAR");
    } else if (type == S_ZEROFILL) {
        strcpy(out, "S_ZEROFILL");
    } else if (type == S_CSTRING_LITERALS) {
        strcpy(out, "S_CSTRING_LITERALS");
    } else if (type == S_4BYTE_LITERALS) {
        strcpy(out, "S_4BYTE_LITERALS");
    } else if (type == S_8BYTE_LITERALS) {
        strcpy(out, "S_8BYTE_LITERALS");
    } else if (type == S_LITERAL_POINTERS) {
        strcpy(out, "S_LITERAL_POINTERS");
    } else if (type == S_NON_LAZY_SYMBOL_POINTERS) {
        strcpy(out, "S_NON_LAZY_SYMBOL_POINTERS");
    } else if (type == S_LAZY_SYMBOL_POINTERS) {
        strcpy(out, "S_LAZY_SYMBOL_POINTERS");
    } else if (type == S_LITERAL_POINTERS) {
        strcpy(out, "S_LITERAL_POINTERS");
    } else if (type == S_SYMBOL_STUBS) {
        strcpy(out, "S_SYMBOL_STUBS");
    } else {
        sprintf(out, "OTHER(0x%x)", type);
    }
}

void format_n_desc(uint16_t n_desc, char *formatted) {
    strcpy(formatted, "");

    if (n_desc & N_NO_DEAD_STRIP) {
        strcat(formatted, " N_NO_DEAD_STRIP");
    }
    if (n_desc & N_WEAK_REF) {
        strcat(formatted, " N_WEAK_REF");
    }
    if (n_desc & N_WEAK_DEF) {
        strcat(formatted, " N_WEAK_DEF");
    }

    int library_ordinal = GET_LIBRARY_ORDINAL(n_desc);
    if (library_ordinal > 0) {
        sprintf(formatted + strlen(formatted), " LIBRARY_ORDINAL(%d)", library_ordinal);
    }
}

// If the string contains '\n', replace with literal "\n".s
void format_string(char *str, char *formatted) {
    int j = 0;
    for (int i = 0; str[i] != '\0'; ++i) {
        switch(str[i]) {
            case '\n':
                formatted[j++] = '\\';
                formatted[j++] = 'n';
                break;
            default:
                formatted[j++] = str[i];
                break;
        }
    }
    formatted[j] = '\0';
}
