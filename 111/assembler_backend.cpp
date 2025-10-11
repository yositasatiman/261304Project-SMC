// assembler_backend_week3.cpp
// Build: g++ -std=c++17 -O2 assembler_backend_week3.cpp -o asm_w3

#include <iostream>       // cout, cin
#include <string>         // std::string
#include <vector>         // std::vector
#include <unordered_map>  // std::unordered_map (symbol table)
#include <iomanip>        // setw (ใช้เวลา format output)
#include <cctype>         // isdigit, isxdigit
#include <stdexcept>      // สำหรับ throw exception (parse number)
#include <cstdint>        // int32_t, uint32_t

using namespace std;

// -------------------- Opcodes --------------------
enum class Op : int {
    ADD=0, NAND=1, LW=2, SW=3, BEQ=4, JALR=5, HALT=6, NOOP=7, FILL=-1
};
static const unordered_map<string, Op> OPCODE_MAP = {
    {"add",  Op::ADD},  {"nand", Op::NAND},
    {"lw",   Op::LW},   {"sw",   Op::SW},
    {"beq",  Op::BEQ},  {"jalr", Op::JALR},
    {"halt", Op::HALT}, {"noop", Op::NOOP},
    {".fill",Op::FILL}
};

// -------------------- Bit layout --------------------
constexpr int OPCODE_SHIFT = 22;
constexpr int REGA_SHIFT   = 19;
constexpr int REGB_SHIFT   = 16;
inline uint32_t packR(int opcode, int rA, int rB, int dest) {
    return (uint32_t(opcode) << OPCODE_SHIFT)
        | (uint32_t(rA)     << REGA_SHIFT)
        | (uint32_t(rB)     << REGB_SHIFT)
        | (uint32_t(dest) & 0x7u);
}
inline uint32_t packI(int opcode, int rA, int rB, int offset16) {
    return (uint32_t(opcode) << OPCODE_SHIFT)
        | (uint32_t(rA)     << REGA_SHIFT)
        | (uint32_t(rB)     << REGB_SHIFT)
        | (uint32_t(offset16) & 0xFFFFu); // เก็บ 16 บิตล่าง (two's complement)
}
inline uint32_t packJ(int opcode, int rA, int rB) {
    return (uint32_t(opcode) << OPCODE_SHIFT)
        | (uint32_t(rA)     << REGA_SHIFT)
        | (uint32_t(rB)     << REGB_SHIFT);
}
inline uint32_t packO(int opcode) {
    return (uint32_t(opcode) << OPCODE_SHIFT);
}

// -------------------- Errors --------------------
enum class AsmError {
    NONE = 0,
    UNKNOWN_OPCODE,
    UNDEFINED_LABEL,
    OFFSET_OUT_OF_RANGE,
    BAD_IMMEDIATE,
    BAD_REGISTER
};
struct ErrInfo {
    AsmError code{AsmError::NONE};
    string   msg;
};
inline bool okReg(int r){ return 0<=r && r<=7; } // 3-bit regs

// -------------------- Helpers (Week 1–2 ของคุณ) --------------------
ErrInfo toOpcode(const string& mnemonic, int& outOpcode) {
    auto it = OPCODE_MAP.find(mnemonic);
    if (it == OPCODE_MAP.end())
        return {AsmError::UNKNOWN_OPCODE, "unknown opcode: " + mnemonic};
    if (it->second == Op::FILL) { outOpcode = -1; return {AsmError::NONE,""}; }
    outOpcode = static_cast<int>(it->second);
    return {AsmError::NONE,""};
}

inline bool inSigned16(long long x){ return -32768<=x && x<=32767; }

bool looksNumber(const string& s){
    if (s.empty()) return false;
    size_t i=0; if (s[0]=='+'||s[0]=='-') i=1;
    if (i>=s.size()) return false;
    if (i+1<s.size() && s[i]=='0' && (s[i+1]=='x'||s[i+1]=='X')){
        i+=2; if (i>=s.size()) return false;
        for(; i<s.size(); ++i) if(!isxdigit((unsigned char)s[i])) return false;
        return true;
    }
    for(; i<s.size(); ++i) if(!isdigit((unsigned char)s[i])) return false;
    return true;
}
ErrInfo parseNumber(const string& token, long long& outVal){
    if (!looksNumber(token))
        return {AsmError::BAD_IMMEDIATE, "not a valid number: " + token};
    try{
        size_t pos=0;
        outVal = stoll(token, &pos, 0); // auto base (0x..)
        if (pos!=token.size()) return {AsmError::BAD_IMMEDIATE, "trailing junk: " + token};
        return {AsmError::NONE,""};
    }catch(...){
        return {AsmError::BAD_IMMEDIATE, "cannot parse: " + token};
    }
}

ErrInfo findLabel(const unordered_map<string,int>& symtab,
                const string& label, int& outAddr){
    auto it = symtab.find(label);
    if (it==symtab.end()) return {AsmError::UNDEFINED_LABEL, "undefined label: " + label};
    outAddr = it->second; return {AsmError::NONE,""};
}

// สำหรับ I-type: asOffset16=true → ต้องเช็คช่วง 16-bit; isBranch=true → ใช้ relative
ErrInfo getFieldValue(const unordered_map<string,int>& symtab,
                    const string& token, int currentPC,
                    bool asOffset16, bool isBranch, int& outVal){
    long long val=0;
    if (looksNumber(token)){
        auto e=parseNumber(token,val); if(e.code!=AsmError::NONE) return e;
    }else{
        int addr=0; auto e=findLabel(symtab, token, addr);
        if (e.code!=AsmError::NONE) return e;
        if (isBranch) val = (long long)addr - (long long)(currentPC+1);
        else          val = addr;
    }
    if (asOffset16 && !inSigned16(val))
        return {AsmError::OFFSET_OUT_OF_RANGE, "offset out of 16-bit range: " + to_string(val)};
    outVal = (int)val; return {AsmError::NONE,""};
}

// -------------------- IR จาก Part A (ตัวอย่างโครง) --------------------
struct IRInstr {
    // สมมติ Part A เติมค่าตามนี้
    string mnemonic;      // "add","lw","beq","jalr","halt","noop",".fill"
    int    regA{-1}, regB{-1}, dest{-1}; // ใช้เฉพาะที่จำเป็น (R/J)
    string fieldToken;    // ใช้กับ I-type (offset) หรือ .fill (เลข/label)
    int    pc{-1};        // address ของบรรทัดนี้ (เริ่ม 0)
};

// -------------------- สัปดาห์ที่ 3: assemble() --------------------
struct EncodeResult {
    ErrInfo  error;
    int32_t  word{0};
};
EncodeResult assemble(const unordered_map<string,int>& symtab, const IRInstr& ir){
    EncodeResult r;
    int opcode=-1;
    r.error = toOpcode(ir.mnemonic, opcode);
    if (r.error.code!=AsmError::NONE) return r;

    // .fill (พิเศษ ไม่ใช่ instruction)
    if (opcode<0){
        int val=0;
        auto e = getFieldValue(symtab, ir.fieldToken, ir.pc,
                            /*asOffset16=*/false, /*isBranch=*/false, val);
        if (e.code!=AsmError::NONE){ r.error=e; return r; }
        r.word = val; // เขียนค่าตรง ๆ
        return r;
    }

    // ตรวจเรจิสเตอร์พื้นฐานถ้ามีใช้ (0..7)
    auto needReg = [&](int reg, const string& name)->ErrInfo{
        if (reg<0) return ErrInfo{AsmError::BAD_REGISTER, "missing "+name};
        if (!okReg(reg)) return ErrInfo{AsmError::BAD_REGISTER, "bad "+name+": "+to_string(reg)};
        return ErrInfo{AsmError::NONE,""};
    };

    switch (static_cast<Op>(opcode)){
        case Op::ADD:
        case Op::NAND: {
            // R-type: add/nand regA regB dest
            if (auto e=needReg(ir.regA,"regA"); e.code!=AsmError::NONE) { r.error=e; return r; }
            if (auto e=needReg(ir.regB,"regB"); e.code!=AsmError::NONE) { r.error=e; return r; }
            if (auto e=needReg(ir.dest,"destReg"); e.code!=AsmError::NONE){ r.error=e; return r; }
            r.word = (int32_t)packR(opcode, ir.regA, ir.regB, ir.dest);
            return r;
        }
        case Op::LW:
        case Op::SW: {
            // I-type: lw/sw regA regB offset
            if (auto e=needReg(ir.regA,"regA"); e.code!=AsmError::NONE) { r.error=e; return r; }
            if (auto e=needReg(ir.regB,"regB"); e.code!=AsmError::NONE) { r.error=e; return r; }
            int off=0;
            auto e = getFieldValue(symtab, ir.fieldToken, ir.pc,
                                /*asOffset16=*/true, /*isBranch=*/false, off);
            if (e.code!=AsmError::NONE){ r.error=e; return r; }
            r.word = (int32_t)packI(opcode, ir.regA, ir.regB, off);
            return r;
        }
        case Op::BEQ: {
            // I-type: beq regA regB offset(label) → relative
            if (auto e=needReg(ir.regA,"regA"); e.code!=AsmError::NONE) { r.error=e; return r; }
            if (auto e=needReg(ir.regB,"regB"); e.code!=AsmError::NONE) { r.error=e; return r; }
            int off=0;
            auto e = getFieldValue(symtab, ir.fieldToken, ir.pc,
                                /*asOffset16=*/true, /*isBranch=*/true, off);
            if (e.code!=AsmError::NONE){ r.error=e; return r; }
            r.word = (int32_t)packI(opcode, ir.regA, ir.regB, off);
            return r;
        }
        case Op::JALR: {
            // J-type: jalr regA regB
            if (auto e=needReg(ir.regA,"regA"); e.code!=AsmError::NONE) { r.error=e; return r; }
            if (auto e=needReg(ir.regB,"regB"); e.code!=AsmError::NONE) { r.error=e; return r; }
            r.word = (int32_t)packJ(opcode, ir.regA, ir.regB);
            return r;
        }
        case Op::HALT:
        case Op::NOOP: {
            // O-type
            r.word = (int32_t)packO(opcode);
            return r;
        }
        default:
            r.error = {AsmError::UNKNOWN_OPCODE, "unhandled opcode"};
            return r;
    }
}

// -------------------- เดโม่เล็ก ๆ สำหรับเทส --------------------
int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // symbol table ตัวอย่าง (เหมือนมาจาก Part A)
    unordered_map<string,int> symtab{
        {"start",0}, {"loop",3}, {"data",10}, {"end",20}
    };

    // IR ตัวอย่าง (เหมือน input ที่ Part B จะได้รับ)
    vector<IRInstr> irs = {
        {"add",  0,1,2, "",      0},       // add r0 r1 r2
        {"nand", 1,2,3, "",      1},       // nand r1 r2 r3
        {"lw",   1,2,-1,"data",  2},       // lw r1 r2 data   (offset=addr(data))
        {"sw",   1,2,-1,"0x7F",  3},       // sw r1 r2 0x7F
        {"beq",  1,2,-1,"loop",  4},       // beq r1 r2 loop  (offset = loop-(PC+1))
        {"jalr", 4,5,-1,"",      5},       // jalr r4 r5
        {"halt", -1,-1,-1,"",    6},
        {"noop", -1,-1,-1,"",    7},
        {".fill",-1,-1,-1,"123", 8},       // data literal
        {".fill",-1,-1,-1,"end", 9}        // data = address of 'end'
    };

    for (auto& ir : irs){
        auto res = assemble(symtab, ir);
        if (res.error.code != AsmError::NONE){
            cout << setw(6) << ir.mnemonic << " @PC=" << ir.pc
                << "  ERROR: " << res.error.msg << "\n";
        }else{
            cout << setw(6) << ir.mnemonic << " @PC=" << ir.pc
                << "  -> machine code (dec) = " << res.word << "\n";
        }
    }
    return 0;
}
