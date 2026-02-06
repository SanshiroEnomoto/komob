
#include <iostream>
#include "komob.hpp"


class MemoryRegisterTable: public komob::RegisterTable {
  public:
    MemoryRegisterTable(unsigned size=256): registers(size, 0) {}
    bool read(unsigned address, unsigned & value) override {
        if (address >= registers.size()) {
            return false;
        }
        value = registers[address];
        std::cout << "ModbusRead(" << std::hex << address << ") -> " << value << std::dec << std::endl;
        return true;
    }
    bool write(unsigned address, unsigned value) override {
        std::cout << "ModbusWrite(" << std::hex << address << ", " << value << ")" << std::dec << std::endl;
        if (address >= registers.size()) {
            return false;
        }
        registers[address] = value;
        return true;
    }
  private:
    std::vector<unsigned> registers;
};



int main(int argc, char** argv)
{
    return komob::Server(std::make_shared<MemoryRegisterTable>()).run(argc, argv);
}
