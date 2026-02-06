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

#host, port = '192.168.50.63', 502
host, port = 'localhost', 1502

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
