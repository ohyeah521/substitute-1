/* Pretty trivial, but in its own file to match the other architectures. */
#include "x86/jump-patch.h"

static inline void push_mov_head(void **code, uint64_t imm, bool rax) {
    /* push */
    op8(code, rax ? 0x50 : 0x51);
    /* mov */
#ifdef TARGET_x86_64
    op8(code, 0x48);
    op8(code, rax ? 0xb8 : 0xb9);
    op64(code, imm);
#else
    op8(code, rax ? 0xb8 : 0xb9);
    op32(code, imm);
#endif
}

static inline void push_mov_tail(void **code, bool rax) {
    /* pop */
    op8(code, rax ? 0x58 : 0x59);
}

UNUSED
static void transform_dis_pcrel(struct transform_dis_ctx *ctx, uint64_t dpc,
                                struct arch_pcrel_info info) {
    /* push %reg; mov $dpc, %reg; <orig but with reg instead>; pop %reg */
    /* reg is rcx, or rax if the instruction might be using rcx. */
    bool rax = info.reg == 1;
    void *code = *ctx->rewritten_ptr_ptr;
    push_mov_head(&code, dpc, rax);
    ctx->write_newop_here = code;
    code += ctx->base.op_size;
    push_mov_tail(&code, rax);
    *ctx->rewritten_ptr_ptr = code;
    ctx->base.newop[0] = rax ? 0 : 1;
    ctx->base.modify = true;
}

static void transform_dis_branch(struct transform_dis_ctx *ctx, uint_tptr dpc,
                                 int cc) {
    if (dpc >= ctx->pc_patch_start && dpc < ctx->pc_patch_end) {
        if (dpc == ctx->base.pc + ctx->base.op_size && (cc & CC_CALL)) {
            /* Probably a poor man's PC-rel - 'call .; pop %some'.
             * Push the original address. */
            void *code = *ctx->rewritten_ptr_ptr;
            ctx->write_newop_here = NULL;

            /* push %whatever */
            op8(&code, 0x50);
            /* push %rax; mov $dpc, %rax */
            push_mov_head(&code, dpc, true);
            /* mov %rax, 8(%rsp) / mov %eax, 4(%esp) */
#ifdef TARGET_x86_64
            memcpy(code, ((uint8_t[]) {0x48, 0x8b, 0x44, 0x24, 0x08}), 5);
            code += 5;
#else
            memcpy(code, ((uint8_t[]) {0x89, 0x44, 0x24, 0x04}), 4);
            code += 4;
#endif
            /* pop %rax */
            push_mov_tail(&code, true);

            *ctx->rewritten_ptr_ptr = code;
            return;
        }
        ctx->err = SUBSTITUTE_ERR_FUNC_BAD_INSN_AT_START;
        return;
    }
    void *code = *ctx->rewritten_ptr_ptr;

    ctx->write_newop_here = code;
    code += ctx->base.op_size;

    struct arch_dis_ctx arch;
    uintptr_t source = (uintptr_t) code + 2;
    int size = jump_patch_size(source, dpc, arch, true);
    /* if not taken, jmp past the big jump - this is a bit suboptimal but not that bad */
    op8(&code, 0xeb);
    op8(&code, size);
    make_jump_patch(&code, source, dpc, arch);

    *ctx->rewritten_ptr_ptr = code;
    ctx->base.newop[0] = 2;
    ctx->base.modify = true;

    if (!cc)
        transform_dis_ret(ctx);
}

static void transform_dis_pre_dis(UNUSED struct transform_dis_ctx *ctx) {}
static void transform_dis_post_dis(UNUSED struct transform_dis_ctx *ctx) {}
