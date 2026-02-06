#host, port = '192.168.50.63', 502
host, port = 'localhost', 1502

from pymodbus.client import ModbusTcpClient
client = ModbusTcpClient(host, port=port)

address = 0x10
value = 0xabcd

# Writing to device
reply = client.write_registers(address, [value])
if reply is None or reply.isError():
    print("ERROR")

# Reading from device
reply = client.read_holding_registers(address, count=1)
if reply is None or reply.isError():
    print("ERROR")
else:
    print(hex(reply.registers[0]))
