#include "codegen.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "symbol.hpp"
#include "win_runtime.hpp"

namespace {


std::string escape_asm(const std::string& text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (unsigned char ch : text) {
        if (ch == '"') {
            out += "\\\"";
        } else if (ch == '\\') {
            out += "\\\\";
        } else if (ch >= 0x20 && ch < 0x7F) {
            out += static_cast<char>(ch);
        } else {
            char buffer[5];
            std::snprintf(buffer, sizeof buffer, "\\%03o", ch);
            out += buffer;
        }
    }
    return out;
}

//: Рантайм. Всё честно на ассемблере, libc используется как ABI-слой.
const char* RUNTIME = R"ASM(
    .p2align 4
b_panic:
    push %rbp
    mov %rsp, %rbp
    sub $16, %rsp
    mov %rdi, -8(%rbp)
    xor %edi, %edi
    call fflush@PLT
    mov -8(%rbp), %rdx
    lea .Lfmt_panic(%rip), %rsi
    mov $2, %edi
    xor %eax, %eax
    call dprintf@PLT
    mov $1, %edi
    call exit@PLT

    # --- сборка мусора -------------------------------------------------
    # Консервативный mark-sweep: корни — слова на стеке, блоки — строки.
    # Блок: [next:8][size|mark:8][данные...]

    .p2align 4
    .globl b_gc_alloc
b_gc_alloc:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    mov %rdi, -8(%rbp)
    mov %rdi, %rax
    add $15, %rax
    and $-8, %rax
    mov %rax, -16(%rbp)
    add $16, %rax
    mov %rax, %rdi
    call malloc@PLT
    test %rax, %rax
    jne .Lgc_alloc_ok
    lea .Lmsg_oom(%rip), %rdi
    call b_panic
.Lgc_alloc_ok:
    mov %rax, -24(%rbp)
    mov b_gc_head(%rip), %rcx
    mov %rcx, (%rax)
    mov -16(%rbp), %rcx
    mov %rcx, 8(%rax)
    mov %rax, b_gc_head(%rip)
    mov b_gc_bytes(%rip), %rcx
    add -16(%rbp), %rcx
    mov %rcx, b_gc_bytes(%rip)
    cmp b_gc_limit(%rip), %rcx
    jbe .Lgc_alloc_return
    mov -24(%rbp), %rax
    orq $1, 8(%rax)
    call b_gc_collect
.Lgc_alloc_return:
    mov -24(%rbp), %rax
    add $16, %rax
    leave
    ret

    .p2align 4
b_gc_mark:
    test %rdi, %rdi
    je .Lgc_mark_ret
    mov b_gc_head(%rip), %rax
.Lgc_mark_loop:
    test %rax, %rax
    je .Lgc_mark_ret
    lea 16(%rax), %rcx
    cmp %rcx, %rdi
    jb .Lgc_mark_next
    mov 8(%rax), %rdx
    and $-8, %rdx
    add %rcx, %rdx
    cmp %rdx, %rdi
    jae .Lgc_mark_next
    orq $1, 8(%rax)
    ret
.Lgc_mark_next:
    mov (%rax), %rax
    jmp .Lgc_mark_loop
.Lgc_mark_ret:
    ret

    .p2align 4
b_gc_collect:
    push %rbp
    mov %rsp, %rbp
    sub $64, %rsp
    mov %rsp, %rax
    add $7, %rax
    and $-8, %rax
    mov %rax, -8(%rbp)
.Lgc_scan:
    mov -8(%rbp), %rax
    cmp b_stack_top(%rip), %rax
    jae .Lgc_sweep
    mov (%rax), %rdi
    call b_gc_mark
    addq $8, -8(%rbp)
    jmp .Lgc_scan

.Lgc_sweep:
    movq $0, -16(%rbp)
    lea b_gc_head(%rip), %rax
    mov %rax, -24(%rbp)
.Lgc_sweep_loop:
    mov -24(%rbp), %rsi
    mov (%rsi), %rax
    test %rax, %rax
    je .Lgc_sweep_done
    mov 8(%rax), %rcx
    test $1, %cl
    jne .Lgc_keep
    mov (%rax), %rdx
    mov %rdx, (%rsi)
    mov %rax, %rdi
    call free@PLT
    jmp .Lgc_sweep_loop
.Lgc_keep:
    andq $-8, %rcx
    mov %rcx, 8(%rax)
    add %rcx, -16(%rbp)
    mov %rax, -24(%rbp)
    jmp .Lgc_sweep_loop
.Lgc_sweep_done:
    movq $0, b_gc_bytes(%rip)
    mov -16(%rbp), %rax
    mov %rax, b_gc_live(%rip)
    add %rax, %rax
    cmp $1048576, %rax
    jae .Lgc_limit_ok
    mov $1048576, %rax
.Lgc_limit_ok:
    mov %rax, b_gc_limit(%rip)
    leave
    ret

    .p2align 4
b_gc_realloc:
    test %rdi, %rdi
    jne .Lgc_realloc_old
    mov %rsi, %rdi
    jmp b_gc_alloc
.Lgc_realloc_old:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    mov %rdi, -8(%rbp)
    mov %rsi, -16(%rbp)
    lea -16(%rdi), %rax
    mov 8(%rax), %rcx
    and $-8, %rcx
    cmp %rsi, %rcx
    jae .Lgc_realloc_same
    mov %rsi, %rdi
    call b_gc_alloc
    mov %rax, -24(%rbp)
    mov -8(%rbp), %rsi
    mov %rax, %rdi
    mov -8(%rbp), %rax
    lea -16(%rax), %rax
    mov 8(%rax), %rdx
    and $-8, %rdx
    call memcpy@PLT
    mov -24(%rbp), %rax
    leave
    ret
.Lgc_realloc_same:
    mov -8(%rbp), %rax
    leave
    ret

    .p2align 4
b_div:
    push %rbp
    mov %rsp, %rbp
    test %rsi, %rsi
    jne .Lb_div_ok
    lea .Lmsg_div(%rip), %rdi
    call b_panic
.Lb_div_ok:
    mov %rdi, %rax
    cqto
    idiv %rsi
    leave
    ret

    .p2align 4
b_mod:
    push %rbp
    mov %rsp, %rbp
    test %rsi, %rsi
    jne .Lb_mod_ok
    lea .Lmsg_mod(%rip), %rdi
    call b_panic
.Lb_mod_ok:
    mov %rdi, %rax
    cqto
    idiv %rsi
    mov %rdx, %rax
    leave
    ret

    .p2align 4
b_str_len:
    xor %eax, %eax
.Lb_len_loop:
    movzbl (%rdi), %ecx
    test %cl, %cl
    je .Lb_len_done
    inc %rax
.Lb_len_skip:
    inc %rdi
    movzbl (%rdi), %ecx
    and $0xC0, %ecx
    cmp $0x80, %ecx
    je .Lb_len_skip
    jmp .Lb_len_loop
.Lb_len_done:
    ret

    .p2align 4
b_str_to_int:
    xor %esi, %esi
    mov $10, %edx
    jmp strtoll@PLT

    .p2align 4
b_random:
    push %rbp
    mov %rsp, %rbp
    sub $16, %rsp
    mov %rdi, -8(%rbp)
    cmpq $0, b_random_seeded(%rip)
    jne .Lrnd_ready
    movq $1, b_random_seeded(%rip)
    xor %edi, %edi
    call time@PLT
    mov %rax, -16(%rbp)
    call clock@PLT
    xor %rax, -16(%rbp)
    call getpid@PLT
    xor %rax, -16(%rbp)
    mov -16(%rbp), %edi
    call srand@PLT
.Lrnd_ready:
    call rand@PLT
    mov -8(%rbp), %rcx
    test %rcx, %rcx
    jle .Lrnd_zero
    xor %edx, %edx
    div %rcx
    mov %rdx, %rax
    leave
    ret
.Lrnd_zero:
    xor %eax, %eax
    leave
    ret

    .p2align 4
b_str_concat:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    mov %rdi, -8(%rbp)
    mov %rsi, -16(%rbp)
    call strlen@PLT
    mov %rax, -24(%rbp)
    mov -16(%rbp), %rdi
    call strlen@PLT
    add -24(%rbp), %rax
    add $1, %rax
    mov %rax, %rdi
    call b_gc_alloc
    mov %rax, -32(%rbp)
    mov %rax, %rdi
    mov -8(%rbp), %rsi
    call strcpy@PLT
    mov -32(%rbp), %rdi
    mov -16(%rbp), %rsi
    call strcat@PLT
    mov -32(%rbp), %rax
    leave
    ret

    .p2align 4
b_str_from_int:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    mov %rdi, -8(%rbp)
    mov $32, %edi
    call b_gc_alloc
    mov %rax, -16(%rbp)
    mov %rax, %rdi
    mov $32, %esi
    lea .Lfmt_int_num(%rip), %rdx
    mov -8(%rbp), %rcx
    xor %eax, %eax
    call snprintf@PLT
    mov -16(%rbp), %rax
    leave
    ret

    .p2align 4
b_str_from_float:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    movsd %xmm0, -8(%rbp)
    mov $64, %edi
    call b_gc_alloc
    mov %rax, -16(%rbp)
    mov %rax, %rdi
    mov $64, %esi
    lea .Lfmt_float_num(%rip), %rdx
    movsd -8(%rbp), %xmm0
    mov $1, %eax
    call snprintf@PLT
    mov -16(%rbp), %rax
    leave
    ret

    .p2align 4
b_str_from_bool:
    test %rdi, %rdi
    je .Lb_bool_false
    lea .Lstr_true(%rip), %rax
    ret
.Lb_bool_false:
    lea .Lstr_false(%rip), %rax
    ret

    .p2align 4
b_phrase:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    mov %rdi, -8(%rbp)
    lea .Lphrases(%rip), %rax
    mov %rax, -16(%rbp)
.Lb_ph_loop:
    mov -16(%rbp), %rax
    mov (%rax), %rdi
    test %rdi, %rdi
    je .Lb_ph_none
    mov -8(%rbp), %rsi
    call strcmp@PLT
    test %eax, %eax
    je .Lb_ph_found
    addq $16, -16(%rbp)
    jmp .Lb_ph_loop
.Lb_ph_found:
    mov -16(%rbp), %rax
    mov 8(%rax), %rax
    leave
    ret
.Lb_ph_none:
    lea .Lstr_empty(%rip), %rax
    leave
    ret

    .p2align 4
b_read_line:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    movq $128, -8(%rbp)
    movq $0, -16(%rbp)
    mov $128, %edi
    call b_gc_alloc
    mov %rax, -24(%rbp)
.Lb_read_loop:
    mov -16(%rbp), %rax
    cmp -8(%rbp), %rax
    jl .Lb_read_space
    mov -8(%rbp), %rax
    add %rax, %rax
    mov %rax, -8(%rbp)
    mov -24(%rbp), %rdi
    mov %rax, %rsi
    call b_gc_realloc
    mov %rax, -24(%rbp)
.Lb_read_space:
    mov -24(%rbp), %rsi
    add -16(%rbp), %rsi
    xor %edi, %edi
    mov $1, %edx
    call read@PLT
    cmp $1, %rax
    jne .Lb_read_done
    mov -24(%rbp), %rax
    add -16(%rbp), %rax
    cmpb $10, (%rax)
    je .Lb_read_done
    incq -16(%rbp)
    jmp .Lb_read_loop
.Lb_read_done:
    mov -24(%rbp), %rax
    add -16(%rbp), %rax
    movb $0, (%rax)
    mov -24(%rbp), %rax
    leave
    ret

    .p2align 4
b_read_file:
    push %rbp
    mov %rsp, %rbp
    sub $48, %rsp
    lea .Lmode_r(%rip), %rsi
    call fopen@PLT
    test %rax, %rax
    je .Lb_rf_fail
    mov %rax, -8(%rbp)
    mov %rax, %rdi
    xor %esi, %esi
    mov $2, %edx
    call fseek@PLT
    mov -8(%rbp), %rdi
    call ftell@PLT
    mov %rax, -16(%rbp)
    test %rax, %rax
    jns .Lb_rf_size
    movq $0, -16(%rbp)
.Lb_rf_size:
    mov -8(%rbp), %rdi
    xor %esi, %esi
    xor %edx, %edx
    call fseek@PLT
    mov -16(%rbp), %rdi
    add $1, %rdi
    call b_gc_alloc
    mov %rax, -24(%rbp)
    mov %rax, %rdi
    mov $1, %esi
    mov -16(%rbp), %rdx
    mov -8(%rbp), %rcx
    call fread@PLT
    mov -24(%rbp), %rax
    add -16(%rbp), %rax
    movb $0, (%rax)
    mov -8(%rbp), %rdi
    call fclose@PLT
    mov -24(%rbp), %rax
    leave
    ret
.Lb_rf_fail:
    lea .Lstr_empty(%rip), %rax
    leave
    ret

    .p2align 4
b_write_file:
    push %rbp
    mov %rsp, %rbp
    sub $32, %rsp
    mov %rsi, -8(%rbp)
    lea .Lmode_w(%rip), %rsi
    call fopen@PLT
    test %rax, %rax
    je .Lb_wf_fail
    mov %rax, -16(%rbp)
    mov -8(%rbp), %rdi
    mov -16(%rbp), %rsi
    call fputs@PLT
    mov -16(%rbp), %rdi
    call fclose@PLT
    mov $1, %eax
    leave
    ret
.Lb_wf_fail:
    xor %eax, %eax
    leave
    ret
)ASM";

class Codegen {
public:
    explicit Codegen(Target target) : target_(target) {}

    bool windows() const { return target_ == Target::Windows; }

    std::string run(Program& program) {
        emit_runtime_rodata();

        if (!windows()) {
            emit_gc_globals();
            emit_phrase_table();
            text_ << RUNTIME << "\n";
        }

        for (auto& decl : program.decls) {
            if (auto* constant = dynamic_cast<ConstDecl*>(decl.get())) {
                emit_global_const(*constant);
            }
        }

        for (auto& decl : program.decls) {
            if (auto* func = dynamic_cast<FuncDecl*>(decl.get())) emit_function(*func);
        }

        auto* entry = dynamic_cast<Block*>(program.entry.get());
        if (entry == nullptr) fail("повреждено дерево: береста — не блок");
        emit_main(*entry);

        std::ostringstream out;
        out << text_.str();
        out << "\n    .data\n" << data_.str();
        if (windows()) {
            out << "\n    .section .rdata,\"dr\"\n" << rodata_.str();
        } else {
            out << "\n    .section .rodata\n" << rodata_.str();
            out << "    .section .note.GNU-stack,\"\",@progbits\n";
        }
        return out.str();
    }

private:
    void emit_runtime_rodata() {
        rodata_ << "    .p2align 3\n"
                   ".Lstr_empty:\n    .string \"\"\n"
                   ".Lstr_true:\n    .string \"правда\"\n"
                   ".Lstr_false:\n    .string \"кривда\"\n"
                   ".Lfmt_int:\n    .string \"%ld\\n\"\n"
                   ".Lfmt_float:\n    .string \"%.15g\\n\"\n"
                   ".Lfmt_int_num:\n    .string \"%ld\"\n"
                   ".Lfmt_float_num:\n    .string \"%.15g\"\n"
                   ".Lfmt_panic:\n    .string \"береста: %s\\n\"\n"
                   ".Lmsg_div:\n    .string \"деление на ноль\"\n"
                   ".Lmsg_mod:\n    .string \"остаток от деления на ноль\"\n"
                   ".Lmsg_bounds:\n    .string \"выход за границы массива\"\n"
                   ".Lmsg_oom:\n    .string \"не хватило памяти\"\n"
                   ".Lmsg_null:\n    .string \"пустой указатель\"\n"
                   ".Lmode_r:\n    .string \"r\"\n"
                   ".Lmode_w:\n    .string \"w\"\n";
    }

    void emit_gc_globals() {
        data_ << "    .p2align 3\n"
                 "b_gc_head:\n    .quad 0\n"
                 "b_gc_bytes:\n    .quad 0\n"
                 "b_gc_limit:\n    .quad 1048576\n"
                 "b_gc_live:\n    .quad 0\n"
                 "b_stack_top:\n    .quad 0\n"
                 "b_random_seeded:\n    .quad 0\n"
                 "    .globl b_argc\n"
                 "b_argc:\n    .quad 0\n"
                 "    .globl b_argv\n"
                 "b_argv:\n    .quad 0\n";
    }

    void emit_phrase_table() {
        struct Pair {
            const char* key;
            const char* phrase;
        };
        static const Pair table[] = {
            {"привет", "Здравствуй, путник."},
            {"прощание", "Ступай с миром."},
            {"спасибо", "Благодарствую."},
            {"пожалуйста", "На здоровье."},
            {"да", "Так."},
            {"нет", "Никак нет."},
            {"береста", "Береста шелестит."},
            {"тишина", "В лесу тихо."},
            {"ошибка", "Что-то пошло не так."},
            {"дорога", "Дорога зовёт."},
            {"утро", "Утро доброе."},
            {"вечер", "Вечер тихий."},
        };

        data_ << "    .p2align 3\n.Lphrases:\n";
        for (const Pair& pair : table) {
            const std::string key = intern_string(pair.key);
            const std::string phrase = intern_string(pair.phrase);
            data_ << "    .quad " << key << "\n    .quad " << phrase << "\n";
        }
        data_ << "    .quad 0\n    .quad 0\n";
    }

    // --- мелочи -------------------------------------------------------

    void instruction(const std::string& text) { text_ << "    " << text << "\n"; }

    void label(const std::string& name) { text_ << name << ":\n"; }

    std::string new_label(const std::string& prefix) {
        return ".L" + prefix + "_" + std::to_string(label_counter_++);
    }

    std::string intern_string(const std::string& value) {
        const std::string name = ".Lstr_" + std::to_string(string_counter_++);
        rodata_ << "    .p2align 3\n" << name << ":\n    .string \"" << escape_asm(value)
                << "\"\n";
        return name;
    }

    std::string intern_double(double value) {
        const std::string name = ".Lnum_" + std::to_string(number_counter_++);
        std::ostringstream text;
        text << std::setprecision(17) << value;
        rodata_ << "    .p2align 3\n" << name << ":\n    .double " << text.str() << "\n";
        return name;
    }

    static std::string align16(int bytes) { return std::to_string((bytes + 15) / 16 * 16); }

    //: На Windows два слота заняты сохранёнными rdi/rsi (callee-saved).
    int extra_slots() const { return windows() ? 2 : 0; }

    std::string int_arg(std::size_t index) const {
        static const char* sysv[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
        static const char* win[] = {"%rcx", "%rdx", "%r8", "%r9"};
        return windows() ? win[index] : sysv[index];
    }

    std::string float_arg(std::size_t index) const {
        return "%xmm" + std::to_string(index);
    }

    int max_int_args() const { return windows() ? 4 : 6; }
    int max_float_args() const { return windows() ? 4 : 8; }

    std::string call_symbol(const std::string& name) const {
        if (!windows()) return name;
        if (name.size() > 4 && name.compare(name.size() - 4, 4, "@PLT") == 0) {
            return name.substr(0, name.size() - 4);
        }
        return name;
    }

    std::string slot_ref(int slot) const {
        return std::to_string(-8 * (slot + 1 + extra_slots())) + "(%rbp)";
    }

    std::string mem(const Symbol* symbol) {
        if (symbol->global) return symbol->cname + "(%rip)";
        return slot_ref(symbol->slot);
    }

    //: Адрес нулевого элемента массива. Элементы лежат подряд вверх по стеку.
    std::string array_base(const Symbol* symbol) const {
        return std::to_string(-8 * (symbol->slot + symbol->count + extra_slots())) + "(%rbp)";
    }

    //: Адрес уклада: поля лежат подряд вверх по стеку, как элементы массива.
    std::string struct_base(const Symbol* symbol) const {
        return std::to_string(-8 * (symbol->slot + symbol->layout->size_slots +
                                    extra_slots())) +
               "(%rbp)";
    }

    //: Кладёт адрес массива в %rdx: локального или переданного по ссылке.
    void load_array_base(const Symbol* symbol) {
        if (symbol->by_ref) {
            instruction("mov " + slot_ref(symbol->slot) + ", %rdx");
        } else {
            instruction("lea " + array_base(symbol) + ", %rdx");
        }
    }

    //: Кладёт адрес уклада в %rdx.
    void load_struct_base(const Symbol* symbol) {
        if (symbol->by_ref) {
            instruction("mov " + slot_ref(symbol->slot) + ", %rdx");
        } else {
            instruction("lea " + struct_base(symbol) + ", %rdx");
        }
    }

    // --- кадр вызова --------------------------------------------------

    static int assign_slots(Stmt* stmt, int next) {
        if (auto* node = dynamic_cast<VarDecl*>(stmt)) {
            if (node->symbol) {
                node->symbol->slot = next;
                if (node->symbol->type == TypeKind::Array) {
                    next += node->symbol->count;
                } else if (node->symbol->type == TypeKind::Struct) {
                    next += node->symbol->layout->size_slots;
                } else {
                    next += 1;
                }
            }
            return next;
        }
        if (auto* node = dynamic_cast<Block*>(stmt)) {
            for (auto& inner : node->body) next = assign_slots(inner.get(), next);
            return next;
        }
        if (auto* node = dynamic_cast<If*>(stmt)) {
            next = assign_slots(node->then_branch.get(), next);
            if (node->otherwise) next = assign_slots(node->otherwise.get(), next);
            return next;
        }
        if (auto* node = dynamic_cast<While*>(stmt)) {
            return assign_slots(node->body.get(), next);
        }
        if (auto* node = dynamic_cast<For*>(stmt)) {
            if (node->symbol) node->symbol->slot = next++;
            node->slot_stop = next++;
            node->slot_step = next++;
            return assign_slots(node->body.get(), next);
        }
        if (auto* node = dynamic_cast<ForEach*>(stmt)) {
            if (node->symbol) node->symbol->slot = next++;
            node->counter_slot = next++;
            return assign_slots(node->body.get(), next);
        }
        return next;
    }

    // --- вызовы -------------------------------------------------------

    void call(const std::string& target) {
        int bytes = windows() ? 32 : 0;
        if ((stack_depth_ + bytes / 8) % 2 != 0) bytes += 8;
        if (bytes > 0) {
            instruction("sub $" + std::to_string(bytes) + ", %rsp");
            stack_depth_ += bytes / 8;
        }
        instruction("call " + call_symbol(target));
        if (bytes > 0) {
            instruction("add $" + std::to_string(bytes) + ", %rsp");
            stack_depth_ -= bytes / 8;
        }
    }

    void push_value(TypeKind type) {
        if (type == TypeKind::Float) {
            instruction("sub $8, %rsp");
            instruction("movsd %xmm0, (%rsp)");
        } else {
            instruction("push %rax");
        }
        ++stack_depth_;
    }

    void pop_value(TypeKind type) {
        if (type == TypeKind::Float) {
            instruction("movsd (%rsp), %xmm1");
            instruction("add $8, %rsp");
        } else {
            instruction("pop %rcx");
        }
        --stack_depth_;
    }

    // --- функции ------------------------------------------------------

    void emit_function(FuncDecl& func) {
        int next = 0;
        for (Param& param : func.params) param.symbol->slot = next++;
        next = assign_slots(func.body.get(), next);

        const std::string frame = align16(next * 8);
        const std::string& name = func.symbol->cname;

        text_ << "    .p2align 4\n";
        if (!windows()) text_ << "    .type " << name << ", @function\n";
        label(name);
        emit_prologue(next);

        int int_index = 0;
        int float_index = 0;
        for (Param& param : func.params) {
            const Symbol* symbol = param.symbol;
            if (!symbol->by_ref && symbol->type == TypeKind::Float) {
                if (float_index >= max_float_args()) {
                    fail("слишком много параметров-дробей", param.line, param.col);
                }
                instruction("movsd " + float_arg(static_cast<std::size_t>(float_index++)) +
                            ", " + slot_ref(symbol->slot));
            } else {
                if (int_index >= max_int_args()) {
                    fail("слишком много параметров", param.line, param.col,
                         static_cast<int>(param.name.size()),
                         windows() ? "на Windows максимум 4 обычных параметра"
                                   : "в бересте максимум 6 обычных параметров");
                }
                instruction("mov " + int_arg(static_cast<std::size_t>(int_index++)) + ", " +
                            slot_ref(symbol->slot));
            }
        }

        stack_depth_ = 0;
        loops_.clear();
        return_label_ = new_label("ret");

        emit_block_body(*dynamic_cast<Block*>(func.body.get()));

        label(return_label_);
        emit_epilogue();
        if (!windows()) text_ << "    .size " << name << ", .-" << name << "\n";
        text_ << "\n";
    }

    void emit_prologue(int slots) {
        instruction("push %rbp");
        instruction("mov %rsp, %rbp");
        if (windows()) {
            // rdi и rsi — callee-saved в Microsoft x64
            instruction("push %rdi");
            instruction("push %rsi");
        }
        if (slots > 0) instruction("sub $" + align16(slots * 8) + ", %rsp");
    }

    void emit_epilogue() {
        if (windows()) {
            instruction("mov -8(%rbp), %rdi");
            instruction("mov -16(%rbp), %rsi");
        }
        instruction("leave");
        instruction("ret");
    }

    void emit_main(Block& entry) {
        const int next = assign_slots(&entry, 0);

        text_ << "    .p2align 4\n    .globl main\n";
        if (!windows()) text_ << "    .type main, @function\n";
        label("main");
        emit_prologue(next);

        stack_depth_ = 0;
        loops_.clear();
        return_label_ = new_label("main_ret");
        if (windows()) {
            instruction("mov %rcx, b_argc(%rip)");
            instruction("mov %rdx, b_argv(%rip)");
        } else {
            instruction("mov %rdi, b_argc(%rip)");
            instruction("mov %rsi, b_argv(%rip)");
        }
        instruction("mov %rbp, b_stack_top(%rip)");

        emit_block_body(entry);

        label(return_label_);
        instruction("mov $0, %eax");
        emit_epilogue();
        if (!windows()) text_ << "    .size main, .-main\n";
        text_ << "\n";
    }

    // --- данные -------------------------------------------------------

    void emit_global_const(ConstDecl& decl) {
        data_ << "    .p2align 3\n" << decl.symbol->cname << ":\n";

        switch (decl.declared) {
            case TypeKind::Int: {
                const auto* literal = dynamic_cast<const IntLit*>(decl.value.get());
                if (literal == nullptr) fail("устой должен быть литералом", decl.line, decl.col);
                data_ << "    .quad " << literal->value << "\n";
                break;
            }
            case TypeKind::Bool: {
                const auto* literal = dynamic_cast<const BoolLit*>(decl.value.get());
                if (literal == nullptr) fail("устой должен быть литералом", decl.line, decl.col);
                data_ << "    .quad " << (literal->value ? 1 : 0) << "\n";
                break;
            }
            case TypeKind::Float: {
                double value = 0;
                if (const auto* float_literal = dynamic_cast<const FloatLit*>(decl.value.get())) {
                    value = float_literal->value;
                } else if (const auto* int_literal =
                               dynamic_cast<const IntLit*>(decl.value.get())) {
                    value = static_cast<double>(int_literal->value);
                } else {
                    fail("устой должен быть литералом", decl.line, decl.col);
                }
                std::uint64_t bits = 0;
                std::memcpy(&bits, &value, sizeof bits);
                data_ << "    .quad " << bits << "\n";
                break;
            }
            case TypeKind::String: {
                const auto* literal = dynamic_cast<const StringLit*>(decl.value.get());
                if (literal == nullptr) fail("устой должен быть литералом", decl.line, decl.col);
                data_ << "    .quad " << intern_string(literal->value) << "\n";
                break;
            }
            case TypeKind::Void:
                fail("устой типа ничто", decl.line, decl.col);
            case TypeKind::Array:
                fail("устой-массив не поддерживается", decl.line, decl.col);
            case TypeKind::Struct:
                fail("устой-уклад не поддерживается", decl.line, decl.col);
            case TypeKind::Pointer:
                fail("устой-указатель не поддерживается", decl.line, decl.col);
        }
    }

    // --- операторы ----------------------------------------------------

    void emit_block_body(const Block& block) {
        for (const auto& stmt : block.body) emit_stmt(*stmt);
    }

    void emit_stmt(Stmt& stmt) {
        if (auto* node = dynamic_cast<VarDecl*>(&stmt)) return emit_var(*node);
        if (auto* node = dynamic_cast<Block*>(&stmt)) return emit_block_body(*node);
        if (auto* node = dynamic_cast<Assign*>(&stmt)) return emit_assign(*node);
        if (auto* node = dynamic_cast<Print*>(&stmt)) return emit_print(*node);
        if (auto* node = dynamic_cast<ExprStmt*>(&stmt)) {
            emit_expr(*node->value);
            return;
        }
        if (auto* node = dynamic_cast<If*>(&stmt)) return emit_if(*node);
        if (auto* node = dynamic_cast<While*>(&stmt)) return emit_while(*node);
        if (auto* node = dynamic_cast<For*>(&stmt)) return emit_for(*node);
        if (auto* node = dynamic_cast<ForEach*>(&stmt)) return emit_foreach(*node);
        if (auto* node = dynamic_cast<IndexAssign*>(&stmt)) return emit_index_assign(*node);
        if (auto* node = dynamic_cast<MemberAssign*>(&stmt)) return emit_member_assign(*node);
        if (auto* node = dynamic_cast<DerefAssign*>(&stmt)) return emit_deref_assign(*node);
        if (auto* node = dynamic_cast<Return*>(&stmt)) {
            if (node->value) {
                if (node->value->widen_to_float) {
                    emit_expr_as_float(*node->value);
                } else {
                    emit_expr(*node->value);
                }
            }
            instruction("jmp " + return_label_);
            return;
        }
        if (auto* node = dynamic_cast<Break*>(&stmt)) {
            if (loops_.empty()) fail("«прервать» вне цикла", node->line, node->col);
            instruction("jmp " + loops_.back().break_label);
            return;
        }
        if (auto* node = dynamic_cast<Continue*>(&stmt)) {
            if (loops_.empty()) fail("«продолжить» вне цикла", node->line, node->col);
            instruction("jmp " + loops_.back().continue_label);
            return;
        }
        fail("неизвестный оператор", stmt.line, stmt.col);
    }

    void emit_var(VarDecl& decl) {
        if (decl.symbol->type == TypeKind::Struct) return emit_struct_init(decl);
        if (decl.symbol->type == TypeKind::Array) return emit_array_init(decl);

        if (decl.value) {
            if (decl.value->widen_to_float) {
                emit_expr_as_float(*decl.value);
            } else {
                emit_expr(*decl.value);
            }
        } else {
            switch (decl.declared) {
                case TypeKind::Float:
                    instruction("xorpd %xmm0, %xmm0");
                    break;
                case TypeKind::String:
                    instruction("lea .Lstr_empty(%rip), %rax");
                    break;
                case TypeKind::Int:
                case TypeKind::Bool:
                case TypeKind::Pointer:
                    instruction("xor %eax, %eax");
                    break;
                default:
                    fail("переменная типа ничто", decl.line, decl.col);
            }
        }

        if (decl.declared == TypeKind::Float) {
            instruction("movsd %xmm0, " + mem(decl.symbol));
        } else {
            instruction("mov %rax, " + mem(decl.symbol));
        }
    }

        void emit_struct_default(const StructDef* def, int base_offset) {
        for (const Field& field : def->fields) {
            const int offset = base_offset + 8 * field.offset;
            const std::string slot = std::to_string(offset) + "(%rbp)";

            if (field.type == TypeKind::Struct) {
                emit_struct_default(field.layout, offset);
                continue;
            }

            switch (field.type) {
                case TypeKind::String:
                    instruction("lea .Lstr_empty(%rip), %rax");
                    instruction("mov %rax, " + slot);
                    break;
                case TypeKind::Float:
                    instruction("xorpd %xmm0, %xmm0");
                    instruction("movsd %xmm0, " + slot);
                    break;
                default:
                    instruction("movq $0, " + slot);
                    break;
            }
        }
    }

    void emit_struct_init(VarDecl& decl) {
        const int base =
            -8 * (decl.symbol->slot + decl.symbol->layout->size_slots + extra_slots());
        emit_struct_default(decl.symbol->layout, base);
    }

    void emit_array_init(VarDecl& decl) {
        for (int i = 0; i < decl.symbol->count; ++i) {
            const std::string slot =
                std::to_string(-8 * (decl.symbol->slot + decl.symbol->count - i + extra_slots())) + "(%rbp)";
            switch (decl.symbol->element) {
                case TypeKind::String:
                    instruction("lea .Lstr_empty(%rip), %rax");
                    instruction("mov %rax, " + slot);
                    break;
                case TypeKind::Float:
                    instruction("xorpd %xmm0, %xmm0");
                    instruction("movsd %xmm0, " + slot);
                    break;
                default:
                    instruction("movq $0, " + slot);
                    break;
            }
        }
    }

    //: Проверка выхода за границы. Индекс лежит в reg.
    void emit_bounds(const Node& node, int count, const std::string& reg) {
        const std::string bad = new_label("bounds_bad");
        const std::string ok = new_label("bounds_ok");

        instruction("cmp $0, " + reg);
        instruction("jl " + bad);
        instruction("cmp $" + std::to_string(count) + ", " + reg);
        instruction("jge " + bad);
        instruction("jmp " + ok);

        label(bad);
        instruction("lea .Lmsg_bounds(%rip), %rdi");
        call("b_panic");
        label(ok);

        (void)node;
    }

    void emit_index_assign(IndexAssign& stmt) {
        emit_expr_as_int(*stmt.index);
        push_value(TypeKind::Int);
        if (stmt.value->widen_to_float) {
            emit_expr_as_float(*stmt.value);
        } else {
            emit_expr(*stmt.value);
        }
        pop_value(TypeKind::Int);  // индекс в %rcx

        emit_bounds(stmt, stmt.symbol->count, "%rcx");
        load_array_base(stmt.symbol);

        if (stmt.symbol->element == TypeKind::Float) {
            instruction("movsd %xmm0, (%rdx,%rcx,8)");
        } else {
            instruction("mov %rax, (%rdx,%rcx,8)");
        }
    }

    void emit_assign(Assign& stmt) {
        if (stmt.value->widen_to_float) {
            emit_expr_as_float(*stmt.value);
        } else {
            emit_expr(*stmt.value);
        }
        if (stmt.symbol->type == TypeKind::Float) {
            instruction("movsd %xmm0, " + mem(stmt.symbol));
        } else {
            instruction("mov %rax, " + mem(stmt.symbol));
        }
    }

    void emit_print(Print& stmt) {
        emit_expr(*stmt.value);

        switch (stmt.value->type) {
            case TypeKind::String:
                instruction("mov %rax, " + int_arg(0));
                call("puts@PLT");
                break;
            case TypeKind::Int:
                instruction("mov %rax, " + int_arg(1));
                instruction("lea .Lfmt_int(%rip), " + int_arg(0));
                instruction("xor %eax, %eax");
                call("printf@PLT");
                break;
            case TypeKind::Float:
                if (windows()) {
                    // В Windows x64 дробный аргумент varargs дублируется в GP-регистре.
                    instruction("movq %xmm0, " + int_arg(1));
                }
                instruction("lea .Lfmt_float(%rip), " + int_arg(0));
                instruction("mov $1, %eax");
                call("printf@PLT");
                break;
            case TypeKind::Bool: {
                const std::string is_false = new_label("print_false");
                const std::string done = new_label("print_end");
                instruction("test %rax, %rax");
                instruction("je " + is_false);
                instruction("lea .Lstr_true(%rip), " + int_arg(0));
                call("puts@PLT");
                instruction("jmp " + done);
                label(is_false);
                instruction("lea .Lstr_false(%rip), " + int_arg(0));
                call("puts@PLT");
                label(done);
                break;
            }
            case TypeKind::Void:
                fail("нечего молвить", stmt.line, stmt.col);
            case TypeKind::Array:
                fail("массив нельзя молвить", stmt.line, stmt.col);
            case TypeKind::Struct:
                fail("уклад нельзя молвить", stmt.line, stmt.col);
            case TypeKind::Pointer:
                fail("указатель нельзя молвить", stmt.line, stmt.col);
        }
    }

    void emit_if(If& stmt) {
        const std::string otherwise = new_label("if_else");
        const std::string done = new_label("if_end");

        emit_expr(*stmt.cond);
        instruction("test %rax, %rax");
        instruction("je " + otherwise);

        emit_stmt(*stmt.then_branch);
        instruction("jmp " + done);

        label(otherwise);
        if (stmt.otherwise) emit_stmt(*stmt.otherwise);
        label(done);
    }

    void emit_while(While& stmt) {
        const std::string cond = new_label("while_cond");
        const std::string done = new_label("while_end");

        label(cond);
        emit_expr(*stmt.cond);
        instruction("test %rax, %rax");
        instruction("je " + done);

        loops_.push_back(LoopLabels{done, cond});
        emit_stmt(*stmt.body);
        loops_.pop_back();

        instruction("jmp " + cond);
        label(done);
    }

    void emit_for(For& stmt) {
        const std::string cond = new_label("for_cond");
        const std::string cond_negative = new_label("for_neg");
        const std::string body = new_label("for_body");
        const std::string step = new_label("for_step");
        const std::string done = new_label("for_end");

        emit_expr(*stmt.start);
        instruction("mov %rax, " + slot_ref(stmt.symbol->slot));

        emit_expr(*stmt.stop);
        instruction("mov %rax, " + slot_ref(stmt.slot_stop));

        if (stmt.step) {
            emit_expr(*stmt.step);
        } else {
            instruction("mov $1, %eax");
        }
        instruction("mov %rax, " + slot_ref(stmt.slot_step));

        label(cond);
        instruction("mov " + slot_ref(stmt.slot_step) + ", %rcx");
        instruction("test %rcx, %rcx");
        instruction("js " + cond_negative);
        instruction("mov " + slot_ref(stmt.symbol->slot) + ", %rax");
        instruction("cmp " + slot_ref(stmt.slot_stop) + ", %rax");
        instruction("jg " + done);
        instruction("jmp " + body);

        label(cond_negative);
        instruction("mov " + slot_ref(stmt.symbol->slot) + ", %rax");
        instruction("cmp " + slot_ref(stmt.slot_stop) + ", %rax");
        instruction("jl " + done);

        label(body);
        loops_.push_back(LoopLabels{done, step});
        emit_stmt(*stmt.body);
        loops_.pop_back();

        label(step);
        instruction("mov " + slot_ref(stmt.symbol->slot) + ", %rax");
        instruction("add " + slot_ref(stmt.slot_step) + ", %rax");
        instruction("mov %rax, " + slot_ref(stmt.symbol->slot));
        instruction("jmp " + cond);

        label(done);
    }

    void emit_foreach(ForEach& stmt) {
        const std::string cond = new_label("fe_cond");
        const std::string step = new_label("fe_step");
        const std::string done = new_label("fe_end");
        const int count = stmt.source->count;

        instruction("movq $0, " + slot_ref(stmt.counter_slot));

        label(cond);
        instruction("mov " + slot_ref(stmt.counter_slot) + ", %rax");
        instruction("cmp $" + std::to_string(count) + ", %rax");
        instruction("jge " + done);
        load_array_base(stmt.source);

        if (stmt.symbol->type == TypeKind::Float) {
            instruction("movsd (%rdx,%rax,8), %xmm0");
            instruction("movsd %xmm0, " + slot_ref(stmt.symbol->slot));
        } else {
            instruction("mov (%rdx,%rax,8), %rax");
            instruction("mov %rax, " + slot_ref(stmt.symbol->slot));
        }

        loops_.push_back(LoopLabels{done, step});
        emit_stmt(*stmt.body);
        loops_.pop_back();

        label(step);
        instruction("incq " + slot_ref(stmt.counter_slot));
        instruction("jmp " + cond);
        label(done);
    }

    // --- выражения ----------------------------------------------------

    void emit_expr(Expr& expr) {
        if (auto* node = dynamic_cast<IntLit*>(&expr)) return emit_int(*node);
        if (auto* node = dynamic_cast<FloatLit*>(&expr)) return emit_float_lit(*node);
        if (auto* node = dynamic_cast<StringLit*>(&expr)) return emit_string(*node);
        if (auto* node = dynamic_cast<BoolLit*>(&expr)) return emit_bool(*node);
        if (auto* node = dynamic_cast<Ident*>(&expr)) return emit_ident(*node);
        if (auto* node = dynamic_cast<Unary*>(&expr)) return emit_unary(*node);
        if (auto* node = dynamic_cast<Binary*>(&expr)) return emit_binary(*node);
        if (auto* node = dynamic_cast<Call*>(&expr)) return emit_call(*node);
        if (auto* node = dynamic_cast<Index*>(&expr)) return emit_index(*node);
        if (auto* node = dynamic_cast<Member*>(&expr)) return emit_member(*node);
        fail("неизвестное выражение", expr.line, expr.col);
    }

    void emit_int(IntLit& expr) {
        if (expr.value >= -2147483648LL && expr.value <= 2147483647LL) {
            instruction("mov $" + std::to_string(expr.value) + ", %rax");
        } else {
            instruction("movabsq $" + std::to_string(expr.value) + ", %rax");
        }
    }

    void emit_float_lit(FloatLit& expr) {
        instruction("movsd " + intern_double(expr.value) + "(%rip), %xmm0");
    }

    void emit_string(StringLit& expr) {
        instruction("lea " + intern_string(expr.value) + "(%rip), %rax");
    }

    void emit_bool(BoolLit& expr) {
        instruction(std::string("mov $") + (expr.value ? "1" : "0") + ", %eax");
    }

    void emit_ident(Ident& expr) {
        const Symbol* symbol = expr.symbol;
        if (symbol->type == TypeKind::Array) {
            fail("массив нельзя использовать как значение", expr.line, expr.col);
        }
        if (symbol->type == TypeKind::Struct) {
            fail("уклад нельзя использовать как значение", expr.line, expr.col);
        }
        if (symbol->type == TypeKind::Float) {
            instruction("movsd " + mem(symbol) + ", %xmm0");
        } else {
            instruction("mov " + mem(symbol) + ", %rax");
        }
    }

    void emit_expr_as_int(Expr& expr) {
        emit_expr(expr);
        if (expr.type == TypeKind::Float) instruction("cvttsd2si %xmm0, %rax");
    }

    void emit_expr_as_float(Expr& expr) {
        emit_expr(expr);
        if (expr.type == TypeKind::Int) instruction("cvtsi2sdq %rax, %xmm0");
    }

    void emit_unary(Unary& expr) {
        if (expr.op == "адрес") {
            emit_address_of(*expr.operand);
            return;
        }

        if (expr.op == "по") {
            emit_expr(*expr.operand);
            emit_null_check();
            if (expr.type == TypeKind::Float) {
                instruction("movsd (%rax), %xmm0");
            } else {
                instruction("mov (%rax), %rax");
            }
            return;
        }

        if (expr.op == "!" || expr.op == "не") {
            emit_expr(*expr.operand);
            instruction("test %rax, %rax");
            instruction("sete %al");
            instruction("movzbl %al, %eax");
            return;
        }

        if (expr.type == TypeKind::Float) {
            emit_expr_as_float(*expr.operand);
            instruction("movabsq $0x8000000000000000, %rax");
            instruction("movq %rax, %xmm1");
            instruction("xorpd %xmm1, %xmm0");
            return;
        }

        emit_expr_as_int(*expr.operand);
        instruction("neg %rax");
    }

    //: Проверка указателя на ноль перед разыменованием.
    void emit_null_check() {
        const std::string ok = new_label("ptr_ok");
        instruction("test %rax, %rax");
        instruction("jne " + ok);
        instruction("lea .Lmsg_null(%rip), %rdi");
        call("b_panic");
        label(ok);
    }

        void emit_address_of(Expr& expr) { emit_lvalue_address(expr); }

    void emit_deref_assign(DerefAssign& stmt) {
        emit_expr(*stmt.pointer);
        emit_null_check();
        push_value(TypeKind::Int);
        emit_expr(*stmt.value);
        pop_value(TypeKind::Int);  // указатель в %rcx

        if (stmt.value->type == TypeKind::Float) {
            instruction("movsd %xmm0, (%rcx)");
        } else {
            instruction("mov %rax, (%rcx)");
        }
    }

    void emit_binary(Binary& expr) {
        if (fold_binary(expr)) return;

        const std::string& op = expr.op;

        if (op == "и" || op == "или") return emit_logic(expr);

        if (op == "+" && expr.left->type == TypeKind::String) {
            emit_expr(*expr.left);
            push_value(TypeKind::String);
            emit_expr(*expr.right);
            pop_value(TypeKind::String);  // левая строка в %rcx, правая в %rax
            instruction("mov %rcx, " + int_arg(0));
            instruction("mov %rax, " + int_arg(1));
            call("b_str_concat");
            return;
        }

        if (expr.left->type == TypeKind::String) return emit_string_compare(expr);

        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (expr.left->type == TypeKind::Float || expr.right->type == TypeKind::Float) {
                emit_float_compare(expr);
            } else {
                emit_int_compare(expr);
            }
            return;
        }

        if (expr.type == TypeKind::Float) return emit_float_arith(expr);
        emit_int_arith(expr);
    }

    void emit_logic(Binary& expr) {
        const std::string shortcut = new_label("logic_short");
        const std::string done = new_label("logic_end");

        if (expr.op == "и") {
            emit_expr(*expr.left);
            instruction("test %rax, %rax");
            instruction("je " + shortcut);
            emit_expr(*expr.right);
            instruction("test %rax, %rax");
            instruction("je " + shortcut);
            instruction("mov $1, %eax");
            instruction("jmp " + done);
            label(shortcut);
            instruction("xor %eax, %eax");
        } else {
            emit_expr(*expr.left);
            instruction("test %rax, %rax");
            instruction("jne " + shortcut);
            emit_expr(*expr.right);
            instruction("test %rax, %rax");
            instruction("jne " + shortcut);
            instruction("xor %eax, %eax");
            instruction("jmp " + done);
            label(shortcut);
            instruction("mov $1, %eax");
        }

        label(done);
    }

    //: Свёртка константных выражений: арифметика и сравнения литералов.
    bool fold_binary(Binary& expr) {
        auto* left_int = dynamic_cast<IntLit*>(expr.left.get());
        auto* right_int = dynamic_cast<IntLit*>(expr.right.get());
        auto* left_float = dynamic_cast<FloatLit*>(expr.left.get());
        auto* right_float = dynamic_cast<FloatLit*>(expr.right.get());
        const std::string& op = expr.op;

        if (left_int != nullptr && right_int != nullptr &&
            (expr.type == TypeKind::Int || expr.type == TypeKind::Bool)) {
            const long long a = left_int->value;
            const long long b = right_int->value;
            long long value = 0;

            if (op == "+") value = a + b;
            else if (op == "-") value = a - b;
            else if (op == "*") value = a * b;
            else if (op == "/") {
                if (b == 0) return false;
                value = a / b;
            } else if (op == "%") {
                if (b == 0) return false;
                value = a % b;
            } else if (op == "==") value = a == b;
            else if (op == "!=") value = a != b;
            else if (op == "<") value = a < b;
            else if (op == "<=") value = a <= b;
            else if (op == ">") value = a > b;
            else if (op == ">=") value = a >= b;
            else if (op == "и") value = a && b;
            else if (op == "или") value = a || b;
            else return false;

            instruction("mov $" + std::to_string(value) + ", %rax");
            return true;
        }

        double a = 0;
        double b = 0;
        bool have_floats = false;

        if (left_float != nullptr && right_int != nullptr) {
            a = left_float->value;
            b = static_cast<double>(right_int->value);
            have_floats = true;
        } else if (left_int != nullptr && right_float != nullptr) {
            a = static_cast<double>(left_int->value);
            b = right_float->value;
            have_floats = true;
        } else if (left_float != nullptr && right_float != nullptr) {
            a = left_float->value;
            b = right_float->value;
            have_floats = true;
        }

        if (!have_floats) return false;

        if (expr.type == TypeKind::Bool) {
            bool value = false;
            if (op == "==") value = a == b;
            else if (op == "!=") value = a != b;
            else if (op == "<") value = a < b;
            else if (op == "<=") value = a <= b;
            else if (op == ">") value = a > b;
            else if (op == ">=") value = a >= b;
            else return false;
            instruction(std::string("mov $") + (value ? "1" : "0") + ", %eax");
            return true;
        }

        if (expr.type != TypeKind::Float) return false;

        double value = 0;
        if (op == "+") value = a + b;
        else if (op == "-") value = a - b;
        else if (op == "*") value = a * b;
        else if (op == "/") value = a / b;
        else return false;

        instruction("movsd " + intern_double(value) + "(%rip), %xmm0");
        return true;
    }

    void emit_int_arith(Binary& expr) {
        emit_expr_as_int(*expr.left);
        push_value(TypeKind::Int);
        emit_expr_as_int(*expr.right);
        pop_value(TypeKind::Int);  // левый в %rcx, правый в %rax

        const std::string& op = expr.op;
        if (op == "+") {
            instruction("add %rcx, %rax");
        } else if (op == "-") {
            instruction("sub %rax, %rcx");
            instruction("mov %rcx, %rax");
        } else if (op == "*") {
            instruction("imul %rcx, %rax");
        } else if (op == "/" || op == "%") {
            instruction("mov %rcx, " + int_arg(0));
            instruction("mov %rax, " + int_arg(1));
            call(op == "/" ? "b_div" : "b_mod");
        } else {
            fail("неизвестная целочисленная операция «" + op + "»", expr.line, expr.col);
        }
    }

    void emit_float_arith(Binary& expr) {
        emit_expr_as_float(*expr.left);
        push_value(TypeKind::Float);
        emit_expr_as_float(*expr.right);
        pop_value(TypeKind::Float);  // левый в %xmm1, правый в %xmm0

        const std::string& op = expr.op;
        if (op == "+") {
            instruction("addsd %xmm1, %xmm0");
        } else if (op == "*") {
            instruction("mulsd %xmm1, %xmm0");
        } else if (op == "-") {
            instruction("subsd %xmm0, %xmm1");
            instruction("movapd %xmm1, %xmm0");
        } else if (op == "/") {
            instruction("divsd %xmm0, %xmm1");
            instruction("movapd %xmm1, %xmm0");
        } else {
            fail("неизвестная дробная операция «" + op + "»", expr.line, expr.col);
        }
    }

    static const char* setcc_for(const std::string& op) {
        if (op == "==") return "sete";
        if (op == "!=") return "setne";
        if (op == "<") return "setl";
        if (op == "<=") return "setle";
        if (op == ">") return "setg";
        return "setge";
    }

    void emit_int_compare(Binary& expr) {
        emit_expr_as_int(*expr.left);
        push_value(TypeKind::Int);
        emit_expr_as_int(*expr.right);
        pop_value(TypeKind::Int);

        instruction("cmp %rax, %rcx");
        instruction(std::string(setcc_for(expr.op)) + " %al");
        instruction("movzbl %al, %eax");
    }

    void emit_float_compare(Binary& expr) {
        emit_expr_as_float(*expr.left);
        push_value(TypeKind::Float);
        emit_expr_as_float(*expr.right);
        pop_value(TypeKind::Float);

        instruction("comisd %xmm0, %xmm1");

        const std::string& op = expr.op;
        const char* cc = "sete";
        if (op == ">") cc = "seta";
        else if (op == ">=") cc = "setae";
        else if (op == "<") cc = "setb";
        else if (op == "<=") cc = "setbe";
        else if (op == "!=") cc = "setne";

        instruction(std::string(cc) + " %al");
        instruction("movzbl %al, %eax");
    }

    void emit_string_compare(Binary& expr) {
        emit_expr(*expr.left);
        push_value(TypeKind::String);
        emit_expr(*expr.right);
        pop_value(TypeKind::String);

        instruction("mov %rcx, " + int_arg(0));
        instruction("mov %rax, " + int_arg(1));
        call("strcmp@PLT");
        instruction("cmp $0, %eax");
        instruction(std::string(setcc_for(expr.op)) + " %al");
        instruction("movzbl %al, %eax");
    }

    void emit_index(Index& expr) {
        emit_expr_as_int(*expr.index);
        emit_bounds(expr, expr.count, "%rax");
        load_array_base(expr.symbol);

        if (expr.element == TypeKind::Float) {
            instruction("movsd (%rdx,%rax,8), %xmm0");
        } else {
            instruction("mov (%rdx,%rax,8), %rax");
        }
    }

        void emit_lvalue_address(Expr& expr) {
        if (auto* ident = dynamic_cast<Ident*>(&expr)) {
            Symbol* symbol = ident->symbol;
            if (symbol->type == TypeKind::Struct) {
                if (symbol->by_ref) {
                    instruction("mov " + slot_ref(symbol->slot) + ", %rax");
                } else {
                    instruction("lea " + struct_base(symbol) + ", %rax");
                }
            } else if (symbol->type == TypeKind::Pointer) {
                instruction("mov " + slot_ref(symbol->slot) + ", %rax");
            } else {
                instruction("lea " + slot_ref(symbol->slot) + ", %rax");
            }
            return;
        }

        if (auto* member = dynamic_cast<Member*>(&expr)) {
            emit_lvalue_address(*member->object);
            const Field& field =
                member->base_layout->fields[static_cast<std::size_t>(member->field_index)];
            if (field.offset != 0) {
                instruction("add $" + std::to_string(8 * field.offset) + ", %rax");
            }
            return;
        }

        if (auto* index = dynamic_cast<Index*>(&expr)) {
            emit_expr_as_int(*index->index);
            emit_bounds(*index, index->count, "%rax");
            load_array_base(index->symbol);
            instruction("lea (%rdx,%rax,8), %rax");
            return;
        }

        fail("адрес берётся только у переменной, элемента массива или поля уклада",
             expr.line, expr.col);
    }

    void emit_member(Member& expr) {
        emit_lvalue_address(expr);
        if (expr.type == TypeKind::Float) {
            instruction("movsd (%rax), %xmm0");
        } else {
            instruction("mov (%rax), %rax");
        }
    }

        void emit_member_assign(MemberAssign& stmt) {
        emit_lvalue_address(*stmt.target);
        push_value(TypeKind::Int);

        if (stmt.value->widen_to_float) {
            emit_expr_as_float(*stmt.value);
        } else {
            emit_expr(*stmt.value);
        }
        pop_value(TypeKind::Int);  // адрес поля в %rcx

        if (stmt.target->type == TypeKind::Float) {
            instruction("movsd %xmm0, (%rcx)");
        } else {
            instruction("mov %rax, (%rcx)");
        }
    }

        void emit_object_address(Expr& expr) {
        auto* ident = dynamic_cast<Ident*>(&expr);
        if (ident == nullptr || ident->symbol == nullptr) {
            fail("в функцию можно передать только имя массива или уклада", expr.line, expr.col);
        }
        if (ident->symbol->type == TypeKind::Pointer) {
            instruction("mov " + slot_ref(ident->symbol->slot) + ", %rax");
            return;
        }
        emit_lvalue_address(expr);
    }

    void emit_call(Call& call_expr) {
        if (call_expr.is_builtin) {
            if (call_expr.cname == "#len") {
                instruction("mov $" + std::to_string(call_expr.count) + ", %eax");
                return;
            }
            if (call_expr.cname == "#to_int") {
                emit_expr_as_float(*call_expr.args.at(0));
                instruction("cvttsd2si %xmm0, %rax");
                return;
            }
            if (call_expr.cname == "#to_float") {
                emit_expr_as_int(*call_expr.args.at(0));
                instruction("cvtsi2sdq %rax, %xmm0");
                return;
            }
            if (call_expr.cname == "#live") {
                if (windows()) {
                    call("b_gc_live_bytes");
                } else {
                    instruction("mov b_gc_live(%rip), %rax");
                }
                return;
            }
            if (call_expr.cname.empty()) {
                // к_строке(строка) — тождество
                emit_expr(*call_expr.args.at(0));
                return;
            }
        }

        const int base_depth = stack_depth_;
        for (auto& arg : call_expr.args) {
            if (arg->type == TypeKind::Array || arg->type == TypeKind::Struct) {
                emit_object_address(*arg);
                push_value(TypeKind::Int);
            } else if (arg->widen_to_float) {
                emit_expr_as_float(*arg);
                push_value(TypeKind::Float);
            } else {
                emit_expr(*arg);
                push_value(arg->type);
            }
        }

        const int count = static_cast<int>(call_expr.args.size());
        int base = windows() ? 32 : 0;
        if ((stack_depth_ + base / 8) % 2 != 0) base += 8;
        if (base > 0) {
            instruction("sub $" + std::to_string(base) + ", %rsp");
            stack_depth_ += base / 8;
        }

        int int_index = 0;
        int float_index = 0;
        for (int i = 0; i < count; ++i) {
            const int offset = base + (count - 1 - i) * 8;
            const std::string address = std::to_string(offset) + "(%rsp)";
            const TypeKind type = call_expr.args[static_cast<std::size_t>(i)]->widen_to_float
                                      ? TypeKind::Float
                                      : call_expr.args[static_cast<std::size_t>(i)]->type;

            if (type == TypeKind::Float) {
                if (float_index >= max_float_args()) {
                    fail("слишком много аргументов-дробей", call_expr.line, call_expr.col);
                }
                instruction("movsd " + address + ", " +
                            float_arg(static_cast<std::size_t>(float_index++)));
            } else {
                if (int_index >= max_int_args()) {
                    fail("слишком много аргументов", call_expr.line, call_expr.col, 1,
                         windows() ? "на Windows максимум 4 обычных аргумента"
                                   : "в бересте максимум 6 обычных аргументов");
                }
                instruction("mov " + address + ", " +
                            int_arg(static_cast<std::size_t>(int_index++)));
            }
        }

        instruction("call " + call_symbol(call_expr.cname));

        const int cleanup = base + count * 8;
        if (cleanup > 0) {
            instruction("add $" + std::to_string(cleanup) + ", %rsp");
            stack_depth_ = base_depth;
        }
    }

    struct LoopLabels {
        std::string break_label;
        std::string continue_label;
    };

    std::ostringstream text_;
    std::ostringstream data_;
    std::ostringstream rodata_;
    Target target_ = Target::Linux;
    std::vector<LoopLabels> loops_;
    std::string return_label_;
    int stack_depth_ = 0;
    int label_counter_ = 0;
    int string_counter_ = 0;
    int number_counter_ = 0;
};

}  // namespace

std::string generate(Program& program, Target target) { return Codegen(target).run(program); }

const char* windows_runtime_c() { return WINDOWS_RUNTIME_C; }
