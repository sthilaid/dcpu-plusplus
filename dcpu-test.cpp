#include <cassert>
#include <dcpu.h>
#include <dcpu-lispasm.h>
#include <decoder.h>
#include <sstream>

#define CreateTestCase(name, source, ...)       \
    {                                           \
        TestCase t(name, source);               \
        __VA_ARGS__                             \
        if (!t.TryTest()) return -1;            \
    }

#define Verify(verif) t.AddVerifier([](const DCPU& cpu, const Memory& mem) { return verif; }, \
                                    [](const DCPU& cpu, const Memory& mem){ return string{#verif}; });
#define VerifyEqual(a, b) t.AddVerifier([](const DCPU& cpu, const Memory& mem) { return a == b; }, \
                                        [](const DCPU& cpu, const Memory& mem) { \
                                            const char fmt[] = #a" (0x%04X) == "#b" (0x%04X)"; \
                                            int sz = snprintf(NULL, 0, fmt, a, b); \
                                            char buf[sz + 1];           \
                                            snprintf(buf, sizeof buf, fmt, a, b); \
                                            return string(buf); });

class TestCase {
public:
    using VerifyType = bool(*)(const DCPU& cpu, const Memory& mem);
    using VerifyStrFnType = string(*)(const DCPU& cpu, const Memory& mem);

    const char* m_testName = nullptr;
    string m_lasmSource = "";
    vector<VerifyType> m_verifiers;
    vector<VerifyStrFnType> m_verifiersTxt;

    TestCase(const char* name, string source)
        : m_testName(name)
        , m_lasmSource(source)
        , m_verifiers()
        , m_verifiersTxt()
    {}
    
    void AddVerifier(VerifyType v, VerifyStrFnType vStr) {
        m_verifiers.push_back(v);
        m_verifiersTxt.push_back(vStr);
    }
    bool TryTest() const;
};

bool TestCase::TryTest() const {
    std::basic_stringstream sourceStream{m_lasmSource};
    vector<Token> tokens = LispAsmParser::Tokenize(sourceStream);
    vector<Instruction> instructions = LispAsmParser::ParseTokens(tokens);
    vector<uint8_t> codebytes = Decoder::UnpackBytes(Decoder::Encode(instructions));

    bool test_success = true;
    DCPU cpu;
    Memory mem;
    cpu.Run(mem, codebytes);
    for (int i=0; i < m_verifiers.size(); ++i) {
        bool success = m_verifiers[i](cpu, mem);
        printf("Test %s-%d ", m_testName, i);
        if (success)
            printf("[SUCCESS]\n");
        else {
        failedtest:
            printf("[FAILURE] : %s\n", m_verifiersTxt[i](cpu, mem).c_str());
        }
        test_success &= success;
    }

    return test_success;
}

int main(int argc, char** argv) {
    CreateTestCase("Basic", "(set X 12)\n", VerifyEqual(cpu.GetRegister(Registers_X), 12));

    CreateTestCase("SET",
                   "(set X 12)\n"
                   "(set (ref x) 21)", VerifyEqual(mem[12], 21));

    CreateTestCase("Basic Various",
                   "(set push 14)\n"
                   "(add peek 1)"
                   "(set b 0x7)"
                   "(and b pop)"
                   "(set a (ref sp -1))",
                   VerifyEqual(cpu.GetSP(), 0xFFFF)
                   VerifyEqual(cpu.GetRegister(Registers_B), 7)
                   VerifyEqual(mem[0xFFFE], 15)
                   VerifyEqual(cpu.GetRegister(Registers_A), 15)
                   );

    CreateTestCase("ADD",
                   "(set x 0xFFFF)"
                   "(add x 1)",
                   VerifyEqual(cpu.GetEX(), 1)
                   VerifyEqual(cpu.GetRegister(Registers_A), 0)
                   );

    CreateTestCase("SUB",
                   "(set (ref 555) 10)"
                   "(sub (ref 555) 1)"
                   "(set y 10)"
                   "(sub y 1)"
                   "(sub x 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 0xFFFF)
                   VerifyEqual(cpu.GetEX(), 0xFFFF)
                   VerifyEqual(cpu.GetRegister(Registers_X), static_cast<uint16_t>(-1))
                   VerifyEqual(cpu.GetRegister(Registers_Y), 9)
                   VerifyEqual(mem[555], 9)
                   );

    CreateTestCase("MUL",
                   "(set x 3)"
                   "(mul x x)"
                   "(mul x x)"
                   "(set y 0x8000)"
                   "(mul y 3)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 81)
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0x8000)
                   VerifyEqual(cpu.GetEX(), 0x1)
                   );

    CreateTestCase("MLU",
                   "(set x -1)"
                   "(set y -1)"
                   "(mul x y)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 1)
                   VerifyEqual(cpu.GetRegister(Registers_Y), static_cast<uint16_t>(-1))
                   );

    CreateTestCase("DIV",
                   "(set x 29)"
                   "(set y 3)"
                   "(div x y)"
                   "(div y 0)"
                   "(set i 1)"
                   "(set j 0x400)"
                   "(div i j)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 9)
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0)
                   VerifyEqual(cpu.GetRegister(Registers_I), 0)
                   VerifyEqual(cpu.GetEX(), 64)
                   );

    CreateTestCase("MOD",
                   "(set x 29)"
                   "(set y 3)"
                   "(mod x y)"
                   "(mod y 0)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 2)
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0)
                   );

    CreateTestCase("MDI",
                   "(set x -29)"
                   "(set y 3)"
                   "(mdi x y)"
                   "(mdi y 0)"
                   "(set i 29)"
                   "(set j 3)"
                   "(mdi i j)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), static_cast<uint16_t>(-2))
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0)
                   VerifyEqual(cpu.GetRegister(Registers_I), 2)
                   );

    CreateTestCase("AND",
                   "(set x 0xAA)"
                   "(set y 0xF0)"
                   "(and x y)"
                   "(and y 0)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 0xA0);
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0)
                   );

    CreateTestCase("BOR",
                   "(set x 0xAA)"
                   "(set y 0x55)"
                   "(bor x y)"
                   "(bor y 0xFF)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 0xFF);
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0xFF)
                   );

    CreateTestCase("XOR",
                   "(set x 0xAA)"
                   "(set y 0x55)"
                   "(xor x y)"
                   "(xor y 0xFF)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 0xFF);
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0xAA)
                   );

    CreateTestCase("SHR",
                   "(set x 0xAA)"
                   "(set y 0x55)"
                   "(shr x 1)"
                   "(shr y 1)"
                   "(set i 1)"
                   "(shr i 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 0x55);
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0x2A)
                   VerifyEqual(cpu.GetRegister(Registers_I), 0)
                   VerifyEqual(cpu.GetEX(), 0x8000)
                   );

    CreateTestCase("ASR",
                   "(set x 1)"
                   "(set y 0x8000)"
                   "(asr x 1)"
                   "(asr y 1)"
                   "(set i 0xF)"
                   "(asr i 3)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 0);
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0xC000)
                   VerifyEqual(cpu.GetRegister(Registers_I), 1)
                   VerifyEqual(cpu.GetEX(), 0xE000)
                   );

    CreateTestCase("SHL",
                   "(set x 1)"
                   "(set y 0x8000)"
                   "(shl x 3)"
                   "(shl y 1)"
                   "(set i 0xF)"
                   "(shl i 3)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_X), 8);
                   VerifyEqual(cpu.GetRegister(Registers_Y), 0)
                   VerifyEqual(cpu.GetRegister(Registers_I), 0x78)
                   VerifyEqual(cpu.GetEX(), 0xE000)
                   );

    CreateTestCase("IFB",
                   "(set x 1)"
                   "(set y 2)"
                   "(ifb x y)"
                   "(set i 1)"
                   "(set x 1)"
                   "(set y 3)"
                   "(ifb x y)"
                   "(set j 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 0)
                   VerifyEqual(cpu.GetRegister(Registers_J), 1)
                   );

    CreateTestCase("IFC",
                   "(set x 1)"
                   "(set y 2)"
                   "(ifc x y)"
                   "(set i 1)"
                   "(set x 1)"
                   "(set y 3)"
                   "(ifc x y)"
                   "(set j 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 1)
                   VerifyEqual(cpu.GetRegister(Registers_J), 0)
                   );

    CreateTestCase("IFE",
                   "(set x 1)"
                   "(set y 2)"
                   "(ife x y)"
                   "(set i 1)"
                   "(set x 3)"
                   "(set y 3)"
                   "(ife x y)"
                   "(set j 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 0)
                   VerifyEqual(cpu.GetRegister(Registers_J), 1)
                   );

    CreateTestCase("IFN",
                   "(set x 1)"
                   "(set y 2)"
                   "(ifn x y)"
                   "(set i 1)"
                   "(set x 3)"
                   "(set y 3)"
                   "(ifn x y)"
                   "(set j 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 1)
                   VerifyEqual(cpu.GetRegister(Registers_J), 0)
                   );

    CreateTestCase("IFG",
                   "(set x 1)"
                   "(set y 2)"
                   "(ifg x y)"
                   "(set i 1)"
                   "(set x 2)"
                   "(set y 2)"
                   "(ifg x y)"
                   "(set j 1)"
                   "(set x 2)"
                   "(set y 1)"
                   "(ifg x y)"
                   "(set a 1)"
                   "(set x 1)"
                   "(set y -1)"
                   "(ifg x y)"
                   "(set b 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 0)
                   VerifyEqual(cpu.GetRegister(Registers_J), 0)
                   VerifyEqual(cpu.GetRegister(Registers_A), 1)
                   VerifyEqual(cpu.GetRegister(Registers_B), 0)
                   );

    CreateTestCase("IFA",
                   "(set x 1)"
                   "(set y 2)"
                   "(ifa x y)"
                   "(set i 1)"
                   "(set x 2)"
                   "(set y 2)"
                   "(ifa x y)"
                   "(set j 1)"
                   "(set x 2)"
                   "(set y 1)"
                   "(ifa x y)"
                   "(set a 1)"
                   "(set x 2)"
                   "(set y -1)"
                   "(ifa x y)"
                   "(set b 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 0)
                   VerifyEqual(cpu.GetRegister(Registers_J), 0)
                   VerifyEqual(cpu.GetRegister(Registers_A), 1)
                   VerifyEqual(cpu.GetRegister(Registers_B), 1)
                   );

    CreateTestCase("IFL",
                   "(set x 1)"
                   "(set y 2)"
                   "(ifl x y)"
                   "(set i 1)"
                   "(set x 2)"
                   "(set y 2)"
                   "(ifl x y)"
                   "(set j 1)"
                   "(set x 2)"
                   "(set y 1)"
                   "(ifl x y)"
                   "(set a 1)"
                   "(set x -1)"
                   "(set y 2)"
                   "(ifl x y)"
                   "(set b 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 1)
                   VerifyEqual(cpu.GetRegister(Registers_J), 0)
                   VerifyEqual(cpu.GetRegister(Registers_A), 0)
                   VerifyEqual(cpu.GetRegister(Registers_B), 0)
                   );

    CreateTestCase("IFU",
                   "(set x 1)"
                   "(set y 2)"
                   "(ifu x y)"
                   "(set i 1)"
                   "(set x 2)"
                   "(set y 2)"
                   "(ifu x y)"
                   "(set j 1)"
                   "(set x 2)"
                   "(set y 1)"
                   "(ifu x y)"
                   "(set a 1)"
                   "(set x -1)"
                   "(set y 2)"
                   "(ifu x y)"
                   "(set b 1)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 1)
                   VerifyEqual(cpu.GetRegister(Registers_J), 0)
                   VerifyEqual(cpu.GetRegister(Registers_A), 0)
                   VerifyEqual(cpu.GetRegister(Registers_B), 1)
                   );

    CreateTestCase("ADX",
                   "(set i 0xFFFF)"
                   "(adx i 2)"
                   "(adx j 3)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), 1)
                   VerifyEqual(cpu.GetRegister(Registers_J), 4)
                   );

    CreateTestCase("SBX",
                   "(set i 1)"
                   "(sbx i 2)"
                   "(sbx j 3)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_I), static_cast<uint16_t>(-1))
                   VerifyEqual(cpu.GetRegister(Registers_J), static_cast<uint16_t>(-4))
                   );

    CreateTestCase("STI",
                   "(set j 2)"
                   "(sti a 0xA)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_A), 0xA)
                   VerifyEqual(cpu.GetRegister(Registers_I), 1)
                   VerifyEqual(cpu.GetRegister(Registers_J), 3)
                   );

    CreateTestCase("STD",
                   "(set j 2)"
                   "(std a 0xA)"
                   ,
                   VerifyEqual(cpu.GetRegister(Registers_A), 0xA)
                   VerifyEqual(cpu.GetRegister(Registers_I), 0xFFFF)
                   VerifyEqual(cpu.GetRegister(Registers_J), 1)
                   );

    return 0;
}
