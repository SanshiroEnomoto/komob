<img src="Komob.png" width="10%">
<span style="font-size:200%">Komob: Lightweight Modbus Server</span>

# Overview
## Features

**Komob** is a lightweight Modbus server written in C++, designed to enable control of embedded devices via Modbus/TCP.
It is intended for use in FPGA + Linux (SoC) environments, with minimal dependencies and an emphasis on easy integration into user code.

- **Lightweight Modbus server written in C++**
  - Control devices running on SoCs via Modbus/TCP
  - Suitable for embedded Linux and resource-constrained environments

- **Header-only and self-contained**
  - No external dependencies (relies only on the standard C++ library)
  - Just `#include "modbus.hpp"` to use
  - No library building or linking required; no dependency on build systems like CMake

- **16-bit / 32-bit register support**
  - Combines two Modbus 16-bit words into 32-bit values

- **Supports multiple simultaneous client connections**
  - Implemented with a polling loop; no threads used
  - Register access is serialized, preventing concurrent execution
  - Prioritizes stable operation even in low-resource environments

- **Multiple register handlers can be registered**
  - Multiple register tables can be registered
  - Processed using the Chain-of-Responsibility pattern
  - Enables logically divided address space implementation
  - Supports software virtual registers and access monitors


## Intended Use Cases

- FPGA / SoC-based control devices
- Modbus interfaces for experimental equipment and measurement instruments
- Lightweight Modbus/TCP servers for embedded systems
- Devices for integration with PLCs and SCADA systems


## Design Philosophy

- Simplicity-first implementation
- Designed for long-term continuous operation
- SoC / FPGA-friendly register model
- Extensible architecture (Chain-of-Responsibility)


# Usage
## User Register Table
Clients read and write integer values (16-bit or 32-bit) to registers (Modbus Holding Registers) at specified addresses using the Modbus protocol.
You define your own register table and implement register read/write operations for your device.
The procedure is as follows:

1. Create a register table class by inheriting from `komob::RegisterTable`
2. Implement the read/write methods:
  - Implement the following methods:
    - **`bool read(unsigned address, unsigned& value)`**: Read the register at `address` and store the result in `value`
    - **`bool write(unsigned address, unsigned value)`**: Write `value` to the register at `address`
  - Return `true` if the `address` is valid; otherwise, return `false`
  - On error, throw an exception (any exception will result in a SLAVE_FAILURE response to the client; logging is your responsibility)

### Register Table Implementation Example
Below is an example implementation of 256 memory registers that store written values.

```cpp
#include <iostream>
#include "komob.hpp"

class MemoryRegisterTable : public komob::RegisterTable {
  public:
    MemoryRegisterTable(unsigned size = 256): registers(size, 0) {}

    bool read(unsigned address, unsigned & value) override {
        if (address >= registers.size()) {
            return false;
        }
        value = registers[address];
        return true;
    }

    bool write(unsigned address, unsigned value) override {
        if (address >= registers.size()) {
            return false;
        }
        registers[address] = value;
        return true;
    }

  private:
    std::vector<unsigned> registers;
};
```

If logging is needed, write to `std::cerr` and connect to an appropriate log collection system based on how the server is deployed (see below).

## Server Part
The server listens on port 502 (or a specified port) and allows connected clients to read from and write to the user's register table via the Modbus protocol.

### Standard Configuration
Create an instance of your register table and pass its `shared_ptr` to the server. This can be written in the same file as the register table.
```cpp
int main(int argc, char** argv)
{
    return komob::Server(
        std::make_shared<MemoryRegisterTable>()
    ).run(argc, argv);
}
```
The server's default settings are as follows:

| Parameter | Default | Comment |
|--|--|--|
| Port Number | 502 | Can be changed with the first program parameter (`argv[1]`) |
| Simultaneous Connections | Unlimited | |
| Keepalive Idle | 3600 seconds | Duration of no communication before automatic disconnection |
| Timeout | 1000 milliseconds | Maximum wait time for incomplete Modbus packets |

When a timeout occurs, the server disconnects the client.
To continue processing, the client must reconnect.

### Using Multiple Register Tables
For details, see the Chain-of-Responsibility section.

```cpp
int main(int argc, char** argv)
{
    return (komob::Server()
        .add(std::make_shared<MyRegisterTable1>())
        .add(std::make_shared<MyRegisterTable2>())
    ).run(argc, argv);
}
```

### 16-bit Mode
Although Modbus is a 16-bit protocol, Komob by default combines two data words for 32-bit access.
For compatibility with PLCs and similar systems, you can optionally enable 16-bit access.

```cpp
int main(int argc, char** argv)
{
    return komob::Server(
        std::make_shared<MemoryRegisterTable>(),
        komob::DataWidth::W16
    ).run(argc, argv);
}
```

## Compilation and Startup
Komob consists of a single header file, so there is no need to link libraries or use special build tools.
If your file containing the register table and `main()` function is named `my-modbus-server.cpp`, copy the `komob.hpp` file to the same directory and compile as follows:
```
g++ -o my-modbus-server my-modbus-server.cpp
```
(Alternatively, instead of copying `komob.hpp`, you can specify its location with the `-I` option.)

Simply run the executable to start listening for client connections (multiple connections are supported).
```
./my-modbus-server
```
To use a different port number, specify it as a command-line argument.
```
./my-modbus-server 1502
```

Typically, you would configure the server to start automatically at system boot. Common approaches include:

- **Proper method**
  - Register as a systemd service (logs go to Journal or syslog)
- **Container-based method** (may not be feasible on SoC)
  - Run with Docker / Docker Compose (logs can be flexibly redirected, e.g., to Elasticsearch)
- **Easy method**
  - Add to `/etc/rc.local`
  - Add to `crontab` with `@reboot`
- **Temporary method**
  - Run inside tmux / screen

For specific methods, ask an AI like "I want to automatically run the command `/PATH/TO/CODE/my-modbus-server` at system startup using systemd" and it will teach you how.

## Client Side
In 16-bit mode, common Modbus clients can be used in the standard way.
The same applies in 32-bit mode (default) if all upper 16 bits are 0 and you are not reading/writing multiple registers in a single transaction.
Use Holding Registers, not Input Registers or Coils.
Below is an example of reading and writing a 16-bit value to a single register using pymodbus.

```python
host, port = '192.168.50.63', 502

from pymodbus.client import ModbusTcpClient
client = ModbusTcpClient(host, port=port)

address = 0x10
value = 0xabcd

# Writing a 16bit value to the device
reply = client.write_registers(address, [value])
if reply is None or reply.isError():
    print("ERROR")

# Reading a 16bit value from the device
reply = client.read_holding_registers(address, count=1)
if reply is None or reply.isError():
    print("ERROR")
else:
    print(hex(reply.registers[0]))
```

For using 32-bit values, see the next chapter.


# Handling 32-bit Data
Modbus uses a 16-bit data width, and access to 32-bit data is not defined in the specification.
Komob provides a 32-bit mode (enabled by default) that interprets two consecutive 16-bit values as 32-bit data.
If you are not interested in implementation details, you can skip to the client example at the end of this section.

## Design
### Interpretation of Modbus Address and Quantity in 32-bit Mode

In the Modbus protocol, `quantity` represents the "number of 16-bit words (data size)" per the specification. In 32-bit mode, the `quantity` parameter indicates the size of the data block, and the data block is interpreted as an array of 32-bit values.
Therefore, in 32-bit mode:

- `address` corresponds directly to the register table index (including odd numbers)
- `quantity` must always be even; requests with an odd `quantity` will result in an error
- Data blocks larger than 32 bits are interpreted as arrays of consecutive registers

This approach means the RegisterTable does not need to handle Modbus-specific "16-bit words" or "even address constraints."

#### Mapping Examples by Server Mode
When writing `[ 0x1111, 0x2222, 0x3333, 0x4444 ]` to Modbus address `1000`, the `quantity` (word count) parameter in the Modbus packet is `4`. When the server receives this request, `RegisterTable`'s `write(address, value)` is called as follows:

| address | value, 16-bit mode | value, 32-bit mode | value, 32-bit CDAB mode |
|--|--|--|--|
| 1000 | 0x1111 | 0x11112222 | 0x22221111 |
| 1001 | 0x2222 | 0x33334444 | 0x44443333 |
| 1002 | 0x3333 | - | - |
| 1003 | 0x4444 | - | - |

In other words, for the same data block size, `write()` is called 4 times in 16-bit mode and 2 times in 32-bit mode.
Note that for multi-register access, the register addresses differ depending on the mode.
The choice between 16-bit and 32-bit mode is expected to be fixed at system design time and not changed at runtime. (If runtime switching or mixing is required, consider avoiding multi-register access.)

The same applies to reads: when issuing a read request from address `1000` with `quantity`/`count` of `4`, `read(address, &value)` is called 4 times (16-bit mode) or 2 times (32-bit mode) as shown above, and an array of length `4` is returned to the client. Therefore, in 32-bit mode, the client must reconstruct the values (see the client implementation examples below).

### Protocol-Independent Register Table

The RegisterTable in Komob is an abstraction representing a "logical register array" that is independent of any specific communication protocol.

- RegisterTable behaves as a mapping from integer index to value
- The index is the logical register number described in the device specification
- RegisterTable itself does not define data width
- The `unsigned` type is used as a width-agnostic value container

This design allows the same RegisterTable implementation to be reused across multiple access methods:

- Modbus
- Memory-mapped I/O
- SPI / I2C
- Other future protocols


### Data Width is a View of the Communication Server

The bit width exposed to external clients is determined by the Server's operation mode, not the RegisterTable.
In Komob, you can explicitly set the Server's operation mode to 16-bit or 32-bit.

- **16-bit mode**
  - 1 logical register = 16 bits
  - Transferred as 1 word in Modbus
- **32-bit mode**
  - 1 logical register = 32 bits
  - Transferred as 2 words (16-bit × 2) in Modbus

The Server performs:

- Masking (truncation) at the specified bit width
- Word splitting/combining as needed

on values obtained from the RegisterTable. Values exceeding the specified width are truncated. This is standard behavior in FPGA / SoC environments and is not treated as an error.

## Operational Assumptions

### SoC Environment: 32-bit Is Convenient

For SoC / FPGA applications, 32-bit mode should be convenient for the following reasons:

- Internal registers and MMIO in SoCs are often 32-bit wide
- Logical register numbers naturally match the implementation
- Modbus functions as a simple external interface (view)


### Integration with Existing PLC / SCADA Systems: 16-bit Is Safe

When integrating with existing PLC, SCADA, HMI, or other systems via Modbus, 16-bit mode is the safer choice. Many PLC systems assume Modbus Holding Registers are 16-bit, with 32-bit values represented by combining 2 registers in vendor-specific ways. Using 32-bit mode can cause interoperability issues due to:

- Word order mismatches (ABCD / CDAB, etc.)
- Differences in address boundaries and alignment
- Type definition differences between PLC vendors


## Client Implementation in 32-bit Mode

On the Modbus client side, use standard libraries (e.g., pymodbus) with the following conventions:

- Read/write 2 consecutive 16-bit registers for each 32-bit value
- By default, the upper word comes first (Big Endian, "ABCD" word order)
- Data size is specified as the number of 16-bit words
- Address increments by 1 for each 32-bit value (RegisterTable is a 32-bit array)

### pymodbus Example
For 32-bit access, decompose values into a 16-bit array when writing, and combine every two words when reading.

```python
def write32(client, address, value):
    data = [ (value >> 16) & 0xffff,  value & 0xffff ]
    reply = client.write_registers(address, data)
    return (reply is not None) and (not reply.isError())
    
def read32(client, address):
    reply = client.read_holding_registers(address, count=2)
    if reply is None or reply.isError():
        return None
    return ((reply.registers[0] & 0xffff) << 16) | (reply.registers[1] & 0xffff)

###

host, port = '192.168.50.63', 502

from pymodbus.client import ModbusTcpClient
client = ModbusTcpClient(host, port=port)

address = 0x10
write32(client, address, 0x12345678)

import time
while True:
    value = read32(client, address)
    print(hex(value))

    write32(client, address, value + 1)
    
    time.sleep(1)
```

### SlowPy Example
Using SlowPy, the Python library from [SlowDash](https://github.com/slowproj/slowdash), you can perform 32-bit access directly.

```python
host, port = '192.168.50.63', 502

from slowpy.control import control_system as ctrl
modbus = ctrl.import_control_module('Modbus').modbus(host, port)

reg = modbus.register32(0x10)
reg.set(0x12345678)

import time
while True:
    value = reg.get()
    print(hex(value))
    
    reg.set(value + 1)
    
    time.sleep(1)
```


# Using Multiple Register Tables with Chain-of-Responsibility
## Structure

In Komob, multiple `RegisterTable` instances can be registered in a chain.

```cpp
int main(int argc, char** argv)
{
    return (komob::Server()
        .add(std::make_shared<MyRegisterTable1>())
        .add(std::make_shared<MyRegisterTable2>())
    ).run(argc, argv);
}
```

Requests (read/write) are passed through the tables in order, and processing stops when one succeeds.

- Processing stops when `read()` / `write()` returns `true`
- If `false` is returned, the request is delegated to the next `RegisterTable`

This mechanism enables not only functionally separated register tables, but also "software registers" that add functionality, or cross-cutting concerns such as access monitoring.


## Examples of What You Can Do

- **Functional separation**: Separate tables by address range or purpose for better organization
- **Software registers**: Provide read count, last access time, error counter, etc. as "virtual registers"
- **Monitoring/measurement**: Add access logs, rate measurement, or address tracing without modifying existing implementations
- **Guards/filters**: Reject writes to protected areas, whitelist address ranges, clamp values, etc.

This pattern allows you to start with a minimal implementation and gradually add features as needed.

### 1. Functionally Separated Register Tables

A typical use case is to divide register tables by functional unit:

- Status registers
- Configuration registers
- Control registers
- Debug registers

Implementing each as an independent `RegisterTable` and registering them with the server improves readability and maintainability.

### 2. Software-Only Registers

The Chain-of-Responsibility pattern allows you to naturally add "software registers without physical backing."

For example:

- Virtual registers
- Registers that return computed values
- Registers that trigger other register operations

These can be added without modifying existing register maps.

### 3. Access Monitor Example

Below is an example of a monitor-only RegisterTable for logging Modbus read/write access.

```cpp
class RequestMonitor : public komob::RegisterTable {
  public:
    bool read(unsigned address, unsigned & value) override {
        std::cout << "ModbusRead(" << std::hex << address << ")" << std::dec << std::endl;
        return false;  // Delegate to the next RegisterTable
    }

    bool write(unsigned address, unsigned value) override {
        std::cout << "ModbusWrite(" << std::hex << address << ", " << value << ")" << std::dec << std::endl;
        return false;  // Delegate to the next RegisterTable
    }
};
```

By placing this `RegisterTable` at the head of the chain, you can:

- Log all Modbus access
- Monitor access frequency and usage patterns
- Trace access for debugging

The key point is that this monitor does not affect actual register processing.

### 4. RegisterTable as a Layered Architecture

Using Chain-of-Responsibility, RegisterTables can be organized into layers:

- Monitoring/logging layer
- Virtual/auxiliary register layer
- Physical register layer (directly connected to SoC / FPGA)

Each layer is independent and can be added, removed, or reordered as needed.
