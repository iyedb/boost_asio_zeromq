from __future__ import print_function
import zmq
import time

ADDR='tcp://127.0.0.1:11155'

ctx = zmq.Context()
srv = ctx.socket(zmq.REP)
srv.bind(ADDR)
#srv.setsockopt(zmq.RCVTIMEO, 3000);


while True:
  try:
    msg = srv.recv()
  except Exception as e:
    print('zmq socket revc timedout:', e)
  else:
    print('client says: %s' % msg)
    srv.send('hi from server')
    
  time.sleep(2)
