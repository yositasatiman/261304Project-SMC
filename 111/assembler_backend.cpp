// assembler_backend_week3_fixed.cpp
// จุดประสงค์ไฟล์:
//   - สัปดาห์ที่ 3 (Part B): เข้ารหัสคำสั่งเป็น machine code 32 บิต (R/I/J/O + .fill)
//   - ใช้ IR + symbol table (สมมติ) มาประกอบบิต ตามฟอร์แมตที่กำหนด
//   - โค้ดนี้หลีกเลี่ยง if-with-initializer เพื่อให้คอมไพล์ได้แม้ -std=c++14
//
// วิธีคิดโดยรวม:
//   1) map mnemonic -> opcode
//   2) แยกประเภทคำสั่ง (R/I/J/O/.fill) แล้ว pack บิตด้วย shift/mask
//   3) ฟังก์ชันเสริม: หา label, แปลงตัวเลข, คำนวณ offset (beq = target-(PC+1))
//   4) ตรวจ error สำคัญ: opcode ไม่รู้จัก, reg ผิดช่วง, offset เกิน 16-bit, label ไม่มี
//
// Build:
//   g++ -std=c++17 -O2 assembler_backend_week3_fixed.cpp -o asm_w3
//   (หรือ -std=c++14 ก็ได้)

#include <iostream>       // cout, cin
#include <string>         // std::string
#include <vector>         // std::vector
#include <unordered_map>  // std::unordered_map (symbol table)
#include <iomanip>        // setw (ใช้เวลา format output)
#include <cctype>         // isdigit, isxdigit
#include <stdexcept>      // สำหรับ throw exception (parse number)
#include <cstdint>        // int32_t, uint32_t

using namespace std;

// -------------------- ส่วนกำหนด Opcode --------------------
// หมายเหตุ: .fill = เคสพิเศษ (ไม่ใช่ instruction) เราจะให้เป็นค่า -1
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

// -------------------- Bit layout (การจัดบิต 32 บิต) --------------------
// รูปแบบบิต (ซ้าย->ขวา):
//   [ opcode(3) | regA(3) | regB(3) | ที่เหลือ 16 บิต/3 บิต แล้วแต่ประเภท ]
constexpr int OPCODE_SHIFT = 22;
constexpr int REGA_SHIFT   = 19;
constexpr int REGB_SHIFT   = 16;

// ฟังก์ชัน pack บิตสำหรับแต่ละฟอร์แมต
inline uint32_t packR(int opcode, int rA, int rB, int dest) {
    // R-type: opcode|regA|regB|dest(3 บิต)
    return (uint32_t(opcode) << OPCODE_SHIFT)
         | (uint32_t(rA)     << REGA_SHIFT)
         | (uint32_t(rB)     << REGB_SHIFT)
         | (uint32_t(dest) & 0x7u);
}
inline uint32_t packI(int opcode, int rA, int rB, int offset16) {
    // I-type: opcode|regA|regB|offset(16 บิต - two's complement)
    return (uint32_t(opcode) << OPCODE_SHIFT)
         | (uint32_t(rA)     << REGA_SHIFT)
         | (uint32_t(rB)     << REGB_SHIFT)
         | (uint32_t(offset16) & 0xFFFFu);
}
inline uint32_t packJ(int opcode, int rA, int rB) {
    // J-type: opcode|regA|regB|unused(16)
    return (uint32_t(opcode) << OPCODE_SHIFT)
         | (uint32_t(rA)     << REGA_SHIFT)
         | (uint32_t(rB)     << REGB_SHIFT);
}
inline uint32_t packO(int opcode) {
    // O-type: opcode|unused(22)
    return (uint32_t(opcode) << OPCODE_SHIFT);
}

// -------------------- ส่วน Error / โครงสร้างผลลัพธ์ --------------------
enum class AsmError {
    NONE = 0,
    UNKNOWN_OPCODE,     // ไม่รู้จักคำสั่ง
    UNDEFINED_LABEL,    // อ้าง label ที่ไม่มีใน symbol table
    OFFSET_OUT_OF_RANGE,// offset 16-bit เกินช่วง
    BAD_IMMEDIATE,      // immediate/number แปลงไม่ได้
    BAD_REGISTER        // หมายเลขเรจิสเตอร์ออกนอกช่วง 0..7 หรือหายไป
};

struct ErrInfo {
    AsmError code{AsmError::NONE};
    string   msg;
};

inline bool okReg(int r){ return 0<=r && r<=7; } // reg 3 บิต: 0..7

// -------------------- Helper: mapping/parse/symbol/offset --------------------

// map mnemonic -> opcode (int) (ถ้า .fill จะคืน -1)
ErrInfo toOpcode(const string& mnemonic, int& outOpcode) {
    auto it = OPCODE_MAP.find(mnemonic);
    if (it == OPCODE_MAP.end())
        return {AsmError::UNKNOWN_OPCODE, "unknown opcode: " + mnemonic};
    if (it->second == Op::FILL) { outOpcode = -1; return {AsmError::NONE,""}; }
    outOpcode = static_cast<int>(it->second);
    return {AsmError::NONE,""};
}

// เช็คช่วง signed 16-bit
inline bool inSigned16(long long x){ return -32768<=x && x<=32767; }

// ตรวจลักษณะสตริงว่าเป็นตัวเลขหรือไม่ (รองรับ +/-, dec, 0x..)
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

// แปลงสตริง -> long long (ฐานอัตโนมัติ: 0x.. = 16)
ErrInfo parseNumber(const string& token, long long& outVal){
    if (!looksNumber(token))
        return {AsmError::BAD_IMMEDIATE, "not a valid number: " + token};
    try{
        size_t pos=0;
        outVal = stoll(token, &pos, 0); // base 0 = auto (0x => hex)
        if (pos!=token.size())
            return {AsmError::BAD_IMMEDIATE, "trailing junk: " + token};
        return {AsmError::NONE,""};
    }catch(...){
        return {AsmError::BAD_IMMEDIATE, "cannot parse: " + token};
    }
}

// หา address ของ label จาก symbol table
ErrInfo findLabel(const unordered_map<string,int>& symtab,
                  const string& label, int& outAddr){
    auto it = symtab.find(label);
    if (it==symtab.end())
        return {AsmError::UNDEFINED_LABEL, "undefined label: " + label};
    outAddr = it->second;
    return {AsmError::NONE,""};
}

// คืนค่าฟิลด์ (เลข/label) โดยคำนึงถึงชนิดฟิลด์:
//   - asOffset16=true  => ต้องบีบให้เข้า 16 บิต (ถ้าเกิน => error)
//   - isBranch=true    => คำนวณ relative offset = target - (PC+1) (สำหรับ beq)
//   - isBranch=false   => ใช้ address ตรง ๆ (เช่น lw/sw/.fill เมื่อเป็น label)
ErrInfo getFieldValue(const unordered_map<string,int>& symtab,
                      const string& token, int currentPC,
                      bool asOffset16, bool isBranch, int& outVal){
    long long val=0;
    if (looksNumber(token)){
        ErrInfo e = parseNumber(token,val);
        if(e.code!=AsmError::NONE) return e;
    }else{
        int addr=0;
        ErrInfo e = findLabel(symtab, token, addr);
        if (e.code!=AsmError::NONE) return e;
        if (isBranch) val = (long long)addr - (long long)(currentPC+1);
        else          val = addr;
    }
    if (asOffset16 && !inSigned16(val))
        return {AsmError::OFFSET_OUT_OF_RANGE, "offset out of 16-bit range: " + to_string(val)};
    outVal = (int)val;
    return {AsmError::NONE,""};
}

// -------------------- โครงสร้าง IR (อินพุตจาก Part A) --------------------
// เราจะสมมติว่า Parser (Part A) แยกมาให้แบบนี้
struct IRInstr {
    string mnemonic;      // "add","lw","beq","jalr","halt","noop",".fill"
    int    regA{-1}, regB{-1}, dest{-1}; // ใช้เฉพาะบาง format (R/J ใช้ dest)
    string fieldToken;    // ใช้กับ I-type (.fill ก็ใช้: เก็บเลข/label)
    int    pc{-1};        // address ของบรรทัดนี้ (เริ่ม 0)
};

// -------------------- ฟังก์ชันช่วยเช็คเรจิสเตอร์ --------------------
static ErrInfo needReg(int reg, const string& name){
    if (reg < 0)
        return {AsmError::BAD_REGISTER, "missing "+name};
    if (!okReg(reg))
        return {AsmError::BAD_REGISTER, "bad "+name+": "+to_string(reg)};
    return {AsmError::NONE,""};
}

// -------------------- สัปดาห์ที่ 3: encode เป็น machine code --------------------
struct EncodeResult {
    ErrInfo  error;
    int32_t  word{0};  // machine code base-10 (signed 32 บิตพิมพ์ออกมา)
};

EncodeResult assemble(const unordered_map<string,int>& symtab, const IRInstr& ir){
    EncodeResult r;
    int opcode=-1;

    // 1) แปลง mnemonic -> opcode (หรือ .fill = -1)
    r.error = toOpcode(ir.mnemonic, opcode);
    if (r.error.code!=AsmError::NONE) return r;

    // 2) .fill = ไม่ใช่คำสั่ง ให้คืนค่าเป็นเลข/addr ตรง ๆ
    if (opcode < 0){
        int val=0;
        ErrInfo e = getFieldValue(symtab, ir.fieldToken, ir.pc,
                                  /*asOffset16=*/false, /*isBranch=*/false, val);
        if (e.code!=AsmError::NONE){ r.error=e; return r; }
        r.word = val;
        return r;
    }

    // 3) เลือกทางตามประเภท opcode
    switch (static_cast<Op>(opcode)){
        case Op::ADD:
        case Op::NAND: {
            // R-type: add/nand regA regB dest
            ErrInfo e = needReg(ir.regA, "regA"); if (e.code!=AsmError::NONE){ r.error=e; return r; }
            e = needReg(ir.regB, "regB");         if (e.code!=AsmError::NONE){ r.error=e; return r; }
            e = needReg(ir.dest, "destReg");      if (e.code!=AsmError::NONE){ r.error=e; return r; }
            r.word = (int32_t)packR(opcode, ir.regA, ir.regB, ir.dest);
            return r;
        }

        case Op::LW:
        case Op::SW: {
            // I-type: lw/sw regA regB offset(16 บิต)
            ErrInfo e = needReg(ir.regA, "regA"); if (e.code!=AsmError::NONE){ r.error=e; return r; }
            e = needReg(ir.regB, "regB");         if (e.code!=AsmError::NONE){ r.error=e; return r; }
            int off=0;
            e = getFieldValue(symtab, ir.fieldToken, ir.pc,
                              /*asOffset16=*/true, /*isBranch=*/false, off);
            if (e.code!=AsmError::NONE){ r.error=e; return r; }
            r.word = (int32_t)packI(opcode, ir.regA, ir.regB, off);
            return r;
        }

        case Op::BEQ: {
            // I-type: beq regA regB offset(label) → relative = target - (PC+1)
            ErrInfo e = needReg(ir.regA, "regA"); if (e.code!=AsmError::NONE){ r.error=e; return r; }
            e = needReg(ir.regB, "regB");         if (e.code!=AsmError::NONE){ r.error=e; return r; }
            int off=0;
            e = getFieldValue(symtab, ir.fieldToken, ir.pc,
                              /*asOffset16=*/true, /*isBranch=*/true, off);
            if (e.code!=AsmError::NONE){ r.error=e; return r; }
            r.word = (int32_t)packI(opcode, ir.regA, ir.regB, off);
            return r;
        }

        case Op::JALR: {
            // J-type: jalr regA regB
            ErrInfo e = needReg(ir.regA, "regA"); if (e.code!=AsmError::NONE){ r.error=e; return r; }
            e = needReg(ir.regB, "regB");         if (e.code!=AsmError::NONE){ r.error=e; return r; }
            r.word = (int32_t)packJ(opcode, ir.regA, ir.regB);
            return r;
        }

        case Op::HALT:
        case Op::NOOP: {
            // O-type: ไม่มีเรจิสเตอร์/ออฟเซ็ต
            r.word = (int32_t)packO(opcode);
            return r;
        }

        default:
            r.error = {AsmError::UNKNOWN_OPCODE, "unhandled opcode"};
            return r;
    }
}

// -------------------- เดโม่เล็ก ๆ สำหรับเทส (mock data) --------------------
// หมายเหตุ: ตรงนี้แค่จำลองว่า Part A ส่งอะไรมาให้ เราจะลองเรียก assemble()
//           เพื่อดูว่าผลลัพธ์ machine code base-10 ออกมาถูกตาม format หรือไม่
int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // symbol table ตัวอย่าง (ได้จาก Part A)
    unordered_map<string,int> symtab{
        {"start",0}, {"loop",3}, {"data",10}, {"end",20}
    };

    // IR ตัวอย่าง (เหมือน input จาก Part A)
    vector<IRInstr> irs = {
        {"add",  0,1,2, "",      0},       // add r0 r1 r2
        {"nand", 1,2,3, "",      1},       // nand r1 r2 r3
        {"lw",   1,2,-1,"data",  2},       // lw r1 r2 data   (offset = addr(data))
        {"sw",   1,2,-1,"0x7F",  3},       // sw r1 r2 0x7F
        {"beq",  1,2,-1,"loop",  4},       // beq r1 r2 loop  (offset = loop - (PC+1))
        {"jalr", 4,5,-1,"",      5},       // jalr r4 r5
        {"halt", -1,-1,-1,"",    6},       // halt
        {"noop", -1,-1,-1,"",    7},       // noop
        {".fill",-1,-1,-1,"123", 8},       // data literal
        {".fill",-1,-1,-1,"end", 9}        // data = address of 'end'
    };

    for (const auto& ir : irs){
        EncodeResult res = assemble(symtab, ir);
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
