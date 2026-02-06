#host, port = '192.168.50.63', 502
host, port = 'localhost', 1502

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
