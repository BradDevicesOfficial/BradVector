#include "brad/bradasm.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── Tokenizer ─── */

struct token {
    enum { T_EOF, T_ID, T_NUM, T_COLON, T_COMMA, T_LBRACK, T_RBRACK,
           T_PLUS, T_MINUS, T_NEWLINE,
           T_DIR_ORG, T_DIR_WORD, T_DIR_BYTE, T_DIR_ENTRY,
           T_DIR_COMPRESS, T_DIR_NOCOMPRESS, T_DIR_HALF,
           T_OP_ADD, T_OP_SUB, T_OP_MUL, T_OP_AND, T_OP_OR, T_OP_XOR,
           T_OP_SHL, T_OP_SHR, T_OP_ADDI, T_OP_LDW, T_OP_STW,
           T_OP_BZ, T_OP_BNZ, T_OP_JMP, T_OP_CALL, T_OP_RET,
           T_OP_C_ADD, T_OP_C_SUB, T_OP_C_AND, T_OP_C_OR, T_OP_C_XOR,
           T_OP_C_SHL, T_OP_C_SHR, T_OP_C_ADDI, T_OP_C_LDW, T_OP_C_STW,
           T_OP_C_BZ, T_OP_C_BNZ, T_OP_C_MV, T_OP_C_NOP,
           T_OP_C_JMP, T_OP_C_CALL } kind;
    char text[64];
    int ival;
};

struct lexer {
    const char *src;
    const char *p;
    int line;
    struct token tok;
};

static void lex_init(struct lexer *lx, const char *src)
{
    lx->src = src;
    lx->p = src;
    lx->line = 1;
}

static void lex_skip_ws(struct lexer *lx)
{
    while (*lx->p && (*lx->p == ' ' || *lx->p == '\t')) lx->p++;
}

static int lex_next(struct lexer *lx)
{
    lex_skip_ws(lx);
    char c = *lx->p;
    memset(&lx->tok, 0, sizeof(lx->tok));

    if (c == '\0') { lx->tok.kind = T_EOF; return 0; }
    if (c == '\n') { lx->p++; lx->line++; lx->tok.kind = T_NEWLINE; return 0; }
    if (c == ';') {
        while (*lx->p && *lx->p != '\n') lx->p++;
        if (*lx->p == '\n') { lx->p++; lx->line++; }
        lx->tok.kind = T_NEWLINE;
        return 0;
    }
    if (c == ':') { lx->p++; lx->tok.kind = T_COLON; return 0; }
    if (c == ',') { lx->p++; lx->tok.kind = T_COMMA; return 0; }
    if (c == '[') { lx->p++; lx->tok.kind = T_LBRACK; return 0; }
    if (c == ']') { lx->p++; lx->tok.kind = T_RBRACK; return 0; }
    if (c == '+') { lx->p++; lx->tok.kind = T_PLUS; return 0; }
    if (c == '-') { lx->p++; lx->tok.kind = T_MINUS; return 0; }

    if (c == '.' && lx->p[1]) {
        char buf[32]; unsigned i = 0;
        lx->p++;
        while (*lx->p && (isalpha(*lx->p) || *lx->p == '_') && i < 31) buf[i++] = *lx->p++;
        buf[i] = '\0';
        lx->tok.kind = T_DIR_ORG;
        lx->tok.text[0] = '.';
        strcpy(lx->tok.text + 1, buf);
        if (strcmp(buf, "word") == 0)   lx->tok.kind = T_DIR_WORD;
        else if (strcmp(buf, "byte") == 0)  lx->tok.kind = T_DIR_BYTE;
        else if (strcmp(buf, "org") == 0)   lx->tok.kind = T_DIR_ORG;
        else if (strcmp(buf, "entry") == 0) lx->tok.kind = T_DIR_ENTRY;
        else if (strcmp(buf, "compress") == 0)   lx->tok.kind = T_DIR_COMPRESS;
        else if (strcmp(buf, "nocompress") == 0) lx->tok.kind = T_DIR_NOCOMPRESS;
        else if (strcmp(buf, "half") == 0)       lx->tok.kind = T_DIR_HALF;
        return 0;
    }

    if (c == '.' || isdigit(c)) {
        char buf[32]; unsigned i = 0;
        if (c == '.' && lx->p[1] && !isdigit(lx->p[1])) { /* handled above */ }
        while (*lx->p && (isdigit(*lx->p) || *lx->p == '.')) {
            if (*lx->p == '.' && lx->p[1] == '.') break;
            if (i < 31) buf[i++] = *lx->p;
            lx->p++;
        }
        buf[i] = '\0';
        lx->tok.kind = T_NUM;
        lx->tok.ival = strtol(buf, NULL, 0);
        return 0;
    }

    if (isalpha(c) || c == '_') {
        char buf[64]; unsigned i = 0;
        while (*lx->p && (isalnum(*lx->p) || *lx->p == '_' || *lx->p == '.') && i < 63) buf[i++] = *lx->p++;
        buf[i] = '\0';
        strcpy(lx->tok.text, buf);

        struct { const char *n; int k; } ops[] = {
            {"ADD", T_OP_ADD}, {"SUB", T_OP_SUB}, {"MUL", T_OP_MUL},
            {"AND", T_OP_AND}, {"OR", T_OP_OR},   {"XOR", T_OP_XOR},
            {"SHL", T_OP_SHL}, {"SHR", T_OP_SHR},
            {"ADDI", T_OP_ADDI}, {"LDW", T_OP_LDW}, {"STW", T_OP_STW},
            {"BZ", T_OP_BZ}, {"BNZ", T_OP_BNZ},
            {"JMP", T_OP_JMP}, {"CALL", T_OP_CALL}, {"RET", T_OP_RET},
            {"C.ADD", T_OP_C_ADD}, {"C.SUB", T_OP_C_SUB},
            {"C.AND", T_OP_C_AND}, {"C.OR", T_OP_C_OR}, {"C.XOR", T_OP_C_XOR},
            {"C.SHL", T_OP_C_SHL}, {"C.SHR", T_OP_C_SHR},
            {"C.ADDI", T_OP_C_ADDI}, {"C.LDW", T_OP_C_LDW}, {"C.STW", T_OP_C_STW},
            {"C.BZ", T_OP_C_BZ}, {"C.BNZ", T_OP_C_BNZ},
            {"C.MV", T_OP_C_MV}, {"C.NOP", T_OP_C_NOP},
            {"C.JMP", T_OP_C_JMP}, {"C.CALL", T_OP_C_CALL},
        };
        lx->tok.kind = T_ID;
        for (unsigned i = 0; i < sizeof(ops)/sizeof(ops[0]); i++)
            if (strcmp(buf, ops[i].n) == 0) { lx->tok.kind = ops[i].k; break; }
        return 0;
    }

    snprintf(lx->tok.text, 64, "unexpected char '%c'", c);
    lx->tok.kind = T_EOF;
    return -1;
}

/* ─── Expression evaluator ─── */

struct eval_ctx {
    const struct brad_as_symbol *symbols;
    unsigned nsymbols;
    uint32_t pc;
    int error;
};

static int eval_expr(struct lexer *lx, struct eval_ctx *ec, int *result)
{
    int neg = 0;
    if (lx->tok.kind == T_PLUS) { lex_next(lx); }
    else if (lx->tok.kind == T_MINUS) { neg = 1; lex_next(lx); }

    int val = 0;
    if (lx->tok.kind == T_NUM) {
        val = lx->tok.ival;
        lex_next(lx);
    } else if (lx->tok.kind == T_ID) {
        const char *name = lx->tok.text;
        if (strcmp(name, "$") == 0) {
            val = (int)ec->pc;
        } else {
            int found = 0;
            for (unsigned i = 0; i < ec->nsymbols; i++)
                if (strcmp(ec->symbols[i].name, name) == 0) {
                    val = (int)ec->symbols[i].addr;
                    found = 1;
                    break;
                }
            if (!found) { ec->error = 1; *result = 0; return -1; }
        }
        lex_next(lx);
    } else {
        ec->error = 1;
        *result = 0;
        return -1;
    }
    *result = neg ? -val : val;
    return 0;
}

/* ─── Parse one operand ─── */

static int parse_reg(struct lexer *lx, unsigned *reg)
{
    if (lx->tok.kind == T_ID && lx->tok.text[0] == 'r' && lx->tok.text[1]) {
        unsigned r = (unsigned)strtol(lx->tok.text + 1, NULL, 10);
        if (r < 16) { *reg = r; lex_next(lx); return 0; }
    }
    return -1;
}

/* ─── Halfword emit helpers ─── */

static void emit_half_init(int *half_pos)
{
    *half_pos = 0;
}

static int emit_half(struct brad_as_output *out, unsigned *out_idx,
                     int *half_pos, uint16_t half)
{
    if (*out_idx >= BRAD_ASM_MAX_OUTPUT) return -1;
    if (*half_pos == 0) {
        out->code[*out_idx] = (uint32_t)half;
        *half_pos = 1;
    } else {
        out->code[*out_idx] |= ((uint32_t)half << 16);
        (*out_idx)++;
        *half_pos = 0;
    }
    return 0;
}

static void flush_half(struct brad_as_output *out, unsigned *out_idx,
                       int *half_pos)
{
    if (*half_pos) {
        out->code[*out_idx] |= ((uint32_t)BRAD_CNOP << 16);
        (*out_idx)++;
        *half_pos = 0;
    }
}

/* ─── Main assembler ─── */

int brad_as_assemble(const char *source, struct brad_as_output *out)
{
    memset(out, 0, sizeof(*out));
    out->ok = 1;

    /* ─── Pass 1: collect labels, measure sizes ─── */

    struct lexer lx;
    unsigned pc = 0;
    int compress_mode = 0;

    lex_init(&lx, source);
    for (;;) {
        lex_next(&lx);
        if (lx.tok.kind == T_EOF) break;
        if (lx.tok.kind == T_NEWLINE) continue;

        /* Label? */
        if (lx.tok.kind == T_ID && lx.p[0] == ':') {
            if (out->nsymbols < BRAD_ASM_MAX_SYMBOLS) {
                struct brad_as_symbol *s = &out->symbols[out->nsymbols];
                strncpy(s->name, lx.tok.text, sizeof(s->name) - 1);
                s->addr = pc;
                out->nsymbols++;
            }
            lex_next(&lx); /* consume ID */
            lex_next(&lx); /* consume colon */
            continue;
        }

        /* Label on same line (ID:ID or ID: op) */
        if (lx.tok.kind == T_COLON) {
            lex_next(&lx);
            continue;
        }

        /* Directives */
        int is_dir = 0;
        if (lx.tok.kind == T_DIR_ORG) {
            lex_next(&lx);
            if (lx.tok.kind == T_NUM) { pc = (unsigned)lx.tok.ival; lex_next(&lx); }
            is_dir = 1;
        } else if (lx.tok.kind == T_DIR_WORD) {
            lex_next(&lx);
            pc += 4;
            while (lx.tok.kind != T_NEWLINE && lx.tok.kind != T_EOF) lex_next(&lx);
            is_dir = 1;
        } else if (lx.tok.kind == T_DIR_BYTE) {
            lex_next(&lx);
            pc += 1;
            while (lx.tok.kind != T_NEWLINE && lx.tok.kind != T_EOF) lex_next(&lx);
            is_dir = 1;
        } else if (lx.tok.kind == T_DIR_HALF) {
            lex_next(&lx);
            pc += 2;
            while (lx.tok.kind != T_NEWLINE && lx.tok.kind != T_EOF) lex_next(&lx);
            is_dir = 1;
        } else if (lx.tok.kind == T_DIR_ENTRY) {
            lex_next(&lx);
            is_dir = 1;
        } else if (lx.tok.kind == T_DIR_COMPRESS) {
            compress_mode = 1;
            is_dir = 1;
        } else if (lx.tok.kind == T_DIR_NOCOMPRESS) {
            compress_mode = 0;
            is_dir = 1;
        }

        if (is_dir) continue;

        /* Instructions */
        if (lx.tok.kind >= T_OP_C_ADD && lx.tok.kind <= T_OP_C_CALL)
            pc += 2;
        else if (compress_mode)
            pc += 2;
        else
            pc += 4;

        /* Skip to end of line */
        while (lx.tok.kind != T_NEWLINE && lx.tok.kind != T_EOF) lex_next(&lx);
    }

    /* ─── Pass 2: emit code ─── */

    lex_init(&lx, source);
    pc = 0;
    unsigned out_idx = 0;
    compress_mode = 0;
    int half_pos = 0;
    emit_half_init(&half_pos);

    struct eval_ctx ec;
    ec.symbols = out->symbols;
    ec.nsymbols = out->nsymbols;
    ec.error = 0;

    for (;;) {
        lex_next(&lx);
        if (lx.tok.kind == T_EOF) break;
        if (lx.tok.kind == T_NEWLINE) continue;

        /* Skip labels */
        if (lx.tok.kind == T_ID && lx.p[0] == ':') {
            lex_next(&lx); lex_next(&lx);
            continue;
        }
        if (lx.tok.kind == T_COLON) { lex_next(&lx); continue; }

        uint32_t insn32 = 0;
        uint16_t insn16 = 0;
        int is_insn = 1;
        int emit_16 = 0;
        unsigned rd, rs1, rs2;
        int imm_val = 0;

        if (lx.tok.kind == T_DIR_ORG) {
            lex_next(&lx);
            if (lx.tok.kind == T_NUM) { pc = (unsigned)lx.tok.ival; lex_next(&lx); }
            is_insn = 0;
        } else if (lx.tok.kind == T_DIR_ENTRY) {
            lex_next(&lx);
            if (lx.tok.kind == T_NUM || lx.tok.kind == T_ID) {
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == 0)
                    out->entry_point = (unsigned)imm_val;
            }
            is_insn = 0;
        } else if (lx.tok.kind == T_DIR_COMPRESS) {
            compress_mode = 1;
            is_insn = 0;
        } else if (lx.tok.kind == T_DIR_NOCOMPRESS) {
            flush_half(out, &out_idx, &half_pos);
            compress_mode = 0;
            is_insn = 0;
        } else if (lx.tok.kind == T_DIR_HALF) {
            lex_next(&lx);
            ec.pc = pc;
            if (eval_expr(&lx, &ec, &imm_val) == 0)
                emit_half(out, &out_idx, &half_pos, (uint16_t)(imm_val & 0xFFFF));
            pc += 2;
            is_insn = 0;
        } else if (lx.tok.kind == T_DIR_WORD) {
            lex_next(&lx);
            ec.pc = pc;
            if (eval_expr(&lx, &ec, &imm_val) == 0) {
                flush_half(out, &out_idx, &half_pos);
                if (out_idx < BRAD_ASM_MAX_OUTPUT)
                    out->code[out_idx++] = (uint32_t)imm_val;
            }
            pc += 4;
            is_insn = 0;
        } else if (lx.tok.kind == T_DIR_BYTE) {
            lex_next(&lx);
            ec.pc = pc;
            if (eval_expr(&lx, &ec, &imm_val) == 0) {
                flush_half(out, &out_idx, &half_pos);
                if (out_idx < BRAD_ASM_MAX_OUTPUT)
                    out->code[out_idx++] = (uint32_t)(imm_val & 0xFF);
            }
            pc += 1;
            is_insn = 0;
        } else {
            int op = lx.tok.kind;
            lex_next(&lx);

            switch (op) {
            /* ─── Compressed RRR: C.ADD, C.SUB, C.AND, C.OR, C.XOR, C.SHL, C.SHR ─── */
            case T_OP_C_ADD: case T_OP_C_SUB: case T_OP_C_AND:
            case T_OP_C_OR:  case T_OP_C_XOR:
            case T_OP_C_SHL: case T_OP_C_SHR: {
                if (parse_reg(&lx, &rd) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (parse_reg(&lx, &rs1) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (parse_reg(&lx, &rs2) == -1) goto err;
                static const unsigned cop_rrr[] = {
                    BRAD_COP_ADD, BRAD_COP_SUB, BRAD_COP_AND,
                    BRAD_COP_OR, BRAD_COP_XOR, BRAD_COP_SHL, BRAD_COP_SHR
                };
                insn16 = BRAD_CRRR(cop_rrr[op - T_OP_C_ADD], rd, rs1, rs2);
                emit_16 = 1;
                break;
            }
            /* ─── Compressed ADDI ─── */
            case T_OP_C_ADDI: {
                if (parse_reg(&lx, &rd) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (parse_reg(&lx, &rs1) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                insn16 = BRAD_CRI(BRAD_COP_ADDI, rd, rs1, imm_val & 0xF);
                emit_16 = 1;
                break;
            }
            /* ─── Compressed LDW ─── */
            case T_OP_C_LDW: {
                if (parse_reg(&lx, &rd) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (lx.tok.kind == T_LBRACK) {
                    lex_next(&lx);
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_PLUS || lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                    if (lx.tok.kind == T_RBRACK) lex_next(&lx);
                } else {
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                }
                insn16 = BRAD_CRI(BRAD_COP_LDW, rd, rs1, imm_val & 0xF);
                emit_16 = 1;
                break;
            }
            /* ─── Compressed STW ─── */
            case T_OP_C_STW: {
                if (parse_reg(&lx, &rs2) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (lx.tok.kind == T_LBRACK) {
                    lex_next(&lx);
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_PLUS || lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                    if (lx.tok.kind == T_RBRACK) lex_next(&lx);
                } else {
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                }
                insn16 = BRAD_CRI(BRAD_COP_STW, rs2, rs1, imm_val & 0xF);
                emit_16 = 1;
                break;
            }
            /* ─── Compressed BZ / BNZ ─── */
            case T_OP_C_BZ: case T_OP_C_BNZ: {
                if (parse_reg(&lx, &rs1) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                imm_val -= (int)pc + 2;
                if (imm_val % 2 != 0) imm_val = (imm_val < 0) ? ((imm_val - 1) / 2) : ((imm_val + 1) / 2);
                else imm_val /= 2;
                unsigned cop = (op == T_OP_C_BZ) ? BRAD_COP_BZ : BRAD_COP_BNZ;
                insn16 = BRAD_CBR(cop, rs1, imm_val & 0xFF);
                emit_16 = 1;
                break;
            }
            /* ─── Compressed MV ─── */
            case T_OP_C_MV: {
                if (parse_reg(&lx, &rd) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                insn16 = BRAD_CMV(rd, imm_val & 0xFF);
                emit_16 = 1;
                break;
            }
            /* ─── Compressed NOP ─── */
            case T_OP_C_NOP:
                insn16 = BRAD_CNOP;
                emit_16 = 1;
                break;
            /* ─── Compressed JMP / CALL ─── */
            case T_OP_C_JMP: case T_OP_C_CALL: {
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                imm_val -= (int)pc + 2;
                if (imm_val % 2 != 0) imm_val = (imm_val < 0) ? ((imm_val - 1) / 2) : ((imm_val + 1) / 2);
                else imm_val /= 2;
                unsigned cop = (op == T_OP_C_JMP) ? BRAD_COP_JMP : BRAD_COP_CALL;
                insn16 = BRAD_CBR(cop, 0, imm_val & 0xFF);
                emit_16 = 1;
                break;
            }
            /* ─── V1 RRR: ADD, SUB, MUL, AND, OR, XOR, SHL, SHR ─── */
            case T_OP_ADD: case T_OP_SUB: case T_OP_MUL:
            case T_OP_AND: case T_OP_OR:  case T_OP_XOR:
            case T_OP_SHL: case T_OP_SHR: {
                if (parse_reg(&lx, &rd) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (parse_reg(&lx, &rs1) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (parse_reg(&lx, &rs2) == -1) goto err;
                if (compress_mode) {
                    if (op == T_OP_MUL) {
                        out->ok = 0;
                        snprintf(out->error.msg, sizeof(out->error.msg),
                                 "MUL has no compressed form at line %d (use .nocompress)", lx.line);
                        out->error.line = lx.line;
                        return -1;
                    }
                    static const unsigned v1_to_cop[] = {
                        BRAD_COP_ADD, BRAD_COP_SUB, 0, /* MUL unmapped */
                        BRAD_COP_AND, BRAD_COP_OR, BRAD_COP_XOR,
                        BRAD_COP_SHL, BRAD_COP_SHR
                    };
                    insn16 = BRAD_CRRR(v1_to_cop[op - T_OP_ADD], rd, rs1, rs2);
                    emit_16 = 1;
                } else {
                    insn32 = BRAD_RRR(op - T_OP_ADD, rd, rs1, rs2);
                }
                break;
            }
            /* ─── ADDI rd, rs1, imm ─── */
            case T_OP_ADDI: {
                if (parse_reg(&lx, &rd) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (parse_reg(&lx, &rs1) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                if (compress_mode) {
                    if (imm_val < -8 || imm_val > 7) {
                        out->ok = 0;
                        snprintf(out->error.msg, sizeof(out->error.msg),
                                 "immediate %d out of range for C.ADDI at line %d (use .nocompress)", imm_val, lx.line);
                        out->error.line = lx.line;
                        return -1;
                    }
                    insn16 = BRAD_CRI(BRAD_COP_ADDI, rd, rs1, imm_val & 0xF);
                    emit_16 = 1;
                } else {
                    insn32 = BRAD_RI(BRAD_OP_ADDI, rd, rs1, imm_val);
                }
                break;
            }
            /* ─── LDW rd, [rs1 + imm]  or  LDW rd, rs1, imm ─── */
            case T_OP_LDW: {
                if (parse_reg(&lx, &rd) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (lx.tok.kind == T_LBRACK) {
                    lex_next(&lx);
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_PLUS || lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                    if (lx.tok.kind == T_RBRACK) lex_next(&lx);
                } else {
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                }
                if (compress_mode) {
                    if (imm_val < 0 || imm_val > 15) {
                        out->ok = 0;
                        snprintf(out->error.msg, sizeof(out->error.msg),
                                 "immediate %d out of range for C.LDW at line %d (use .nocompress)", imm_val, lx.line);
                        out->error.line = lx.line;
                        return -1;
                    }
                    insn16 = BRAD_CRI(BRAD_COP_LDW, rd, rs1, imm_val);
                    emit_16 = 1;
                } else {
                    insn32 = BRAD_RI(BRAD_OP_LDW, rd, rs1, imm_val);
                }
                break;
            }
            /* ─── STW rs2, [rs1 + imm]  or  STW rs2, rs1, imm ─── */
            case T_OP_STW: {
                if (parse_reg(&lx, &rs2) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                if (lx.tok.kind == T_LBRACK) {
                    lex_next(&lx);
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_PLUS || lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                    if (lx.tok.kind == T_RBRACK) lex_next(&lx);
                } else {
                    if (parse_reg(&lx, &rs1) == -1) goto err;
                    if (lx.tok.kind == T_COMMA) lex_next(&lx);
                    ec.pc = pc;
                    if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                }
                if (compress_mode) {
                    if (imm_val < 0 || imm_val > 15) {
                        out->ok = 0;
                        snprintf(out->error.msg, sizeof(out->error.msg),
                                 "immediate %d out of range for C.STW at line %d (use .nocompress)", imm_val, lx.line);
                        out->error.line = lx.line;
                        return -1;
                    }
                    insn16 = BRAD_CRI(BRAD_COP_STW, rs2, rs1, imm_val);
                    emit_16 = 1;
                } else {
                    insn32 = (BRAD_OP_STW << 28) | ((rs1) << 20) | ((rs2) << 16) | ((uint16_t)(imm_val & 0xFFFF));
                }
                break;
            }
            /* ─── BZ rs1, offset ─── */
            case T_OP_BZ: {
                if (parse_reg(&lx, &rs1) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                if (compress_mode) {
                    imm_val -= (int)pc + 2;
                    if (imm_val % 2 != 0) imm_val = (imm_val < 0) ? ((imm_val - 1) / 2) : ((imm_val + 1) / 2);
                    else imm_val /= 2;
                    insn16 = BRAD_CBR(BRAD_COP_BZ, rs1, imm_val & 0xFF);
                    emit_16 = 1;
                } else {
                    imm_val -= (int)pc + 4;
                    if (imm_val % 4 != 0) imm_val = (imm_val < 0) ? ((imm_val - 3) / 4) : ((imm_val + 3) / 4);
                    else imm_val /= 4;
                    insn32 = BRAD_BR(BRAD_OP_BZ, rs1, imm_val);
                }
                break;
            }
            /* ─── BNZ rs1, offset ─── */
            case T_OP_BNZ: {
                if (parse_reg(&lx, &rs1) == -1) goto err;
                if (lx.tok.kind == T_COMMA) lex_next(&lx);
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                if (compress_mode) {
                    imm_val -= (int)pc + 2;
                    if (imm_val % 2 != 0) imm_val = (imm_val < 0) ? ((imm_val - 1) / 2) : ((imm_val + 1) / 2);
                    else imm_val /= 2;
                    insn16 = BRAD_CBR(BRAD_COP_BNZ, rs1, imm_val & 0xFF);
                    emit_16 = 1;
                } else {
                    imm_val -= (int)pc + 4;
                    if (imm_val % 4 != 0) imm_val = (imm_val < 0) ? ((imm_val - 3) / 4) : ((imm_val + 3) / 4);
                    else imm_val /= 4;
                    insn32 = BRAD_BR(BRAD_OP_BNZ, rs1, imm_val);
                }
                break;
            }
            /* ─── JMP offset ─── */
            case T_OP_JMP: {
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                if (compress_mode) {
                    imm_val -= (int)pc + 2;
                    if (imm_val % 2 != 0) imm_val = (imm_val < 0) ? ((imm_val - 1) / 2) : ((imm_val + 1) / 2);
                    else imm_val /= 2;
                    insn16 = BRAD_CBR(BRAD_COP_JMP, 0, imm_val & 0xFF);
                    emit_16 = 1;
                } else {
                    imm_val -= (int)pc + 4;
                    if (imm_val % 4 != 0) imm_val = (imm_val < 0) ? ((imm_val - 3) / 4) : ((imm_val + 3) / 4);
                    else imm_val /= 4;
                    insn32 = BRAD_JMP(BRAD_OP_JMP, imm_val);
                }
                break;
            }
            /* ─── CALL offset ─── */
            case T_OP_CALL: {
                ec.pc = pc;
                if (eval_expr(&lx, &ec, &imm_val) == -1) goto err;
                if (compress_mode) {
                    imm_val -= (int)pc + 2;
                    if (imm_val % 2 != 0) imm_val = (imm_val < 0) ? ((imm_val - 1) / 2) : ((imm_val + 1) / 2);
                    else imm_val /= 2;
                    insn16 = BRAD_CBR(BRAD_COP_CALL, 0, imm_val & 0xFF);
                    emit_16 = 1;
                } else {
                    imm_val -= (int)pc + 4;
                    if (imm_val % 4 != 0) imm_val = (imm_val < 0) ? ((imm_val - 3) / 4) : ((imm_val + 3) / 4);
                    else imm_val /= 4;
                    insn32 = BRAD_JMP(BRAD_OP_CALL, imm_val);
                }
                break;
            }
            /* ─── RET ─── */
            case T_OP_RET:
                if (compress_mode) {
                    out->ok = 0;
                    snprintf(out->error.msg, sizeof(out->error.msg),
                             "RET has no compressed form at line %d (use .nocompress)", lx.line);
                    out->error.line = lx.line;
                    return -1;
                }
                insn32 = BRAD_RET_RAW;
                break;
            default:
                goto err;
            }
        }

        if (is_insn) {
            if (emit_16) {
                if (emit_half(out, &out_idx, &half_pos, insn16) == -1)
                    goto err;
                pc += 2;
            } else {
                flush_half(out, &out_idx, &half_pos);
                if (out_idx < BRAD_ASM_MAX_OUTPUT)
                    out->code[out_idx++] = insn32;
                pc += 4;
            }
        }

        /* Consume rest of line */
        while (lx.tok.kind != T_NEWLINE && lx.tok.kind != T_EOF) lex_next(&lx);

        if (ec.error) {
            out->ok = 0;
            snprintf(out->error.msg, sizeof(out->error.msg),
                     "undefined symbol at line %d", lx.line);
            out->error.line = lx.line;
            return -1;
        }

        continue;
err:
        out->ok = 0;
        snprintf(out->error.msg, sizeof(out->error.msg),
                 "syntax error at line %d", lx.line);
        out->error.line = lx.line;
        return -1;
    }

    /* Flush any trailing halfword */
    flush_half(out, &out_idx, &half_pos);

    out->count = out_idx;
    out->ok = 1;
    return 0;
}
