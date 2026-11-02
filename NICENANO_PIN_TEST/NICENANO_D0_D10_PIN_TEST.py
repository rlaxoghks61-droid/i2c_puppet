#!/usr/bin/env python3
"""Windows pin-by-pin multimeter scanner for NICENANO_D0_D10_PIN_TEST.uf2.

Install: py -m pip install pyserial
Run: py "C:\Users\com\Downloads\NICENANO_D0_D10_PIN_TEST.py"
     py "C:\Users\com\Downloads\NICENANO_D0_D10_PIN_TEST.py" --profile nice

External electronics must be disconnected from test pins before driving GPIO.
"""
from __future__ import annotations
import argparse, sys, time

MAPS = {
 'xiao': ['P0.02','P0.03','P0.28','P0.29','P0.04','P0.05',
          'P1.11','P1.12','P1.13','P1.14','P1.15'],
 'nice': ['P0.06','P0.08','P0.17','P0.20','P0.22','P0.24',
          'P1.00','P0.11','P1.04','P1.06','P0.09'],
}

def choose_port(ports, specified):
    if specified: return specified
    candidates = [p.device for p in ports.comports()
                  if p.vid == 0x239A and p.pid == 0x00B4]
    if len(candidates) == 1: return candidates[0]
    if not candidates: raise RuntimeError('No nice!nano normal-mode USB CDC (239A:00B4) found')
    raise RuntimeError('Multiple serial devices: '+', '.join(candidates)+'; specify --port COM12')

def command(dev, cmd):
    dev.reset_input_buffer()
    dev.write((cmd+'\n').encode('ascii'))
    dev.flush()
    deadline = time.monotonic()+4
    while time.monotonic() < deadline:
        line=dev.readline().decode('utf-8',errors='replace').strip()
        if line.startswith(('OK ','PINSCAN ','SKIP ','ERR ','OFF ')): return line
    raise TimeoutError('Firmware did not respond to '+cmd)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--profile',choices=['xiao','nice'],default='xiao')
    ap.add_argument('--port')
    a=ap.parse_args()
    try:
        import serial
        from serial.tools import list_ports
    except ImportError:
        print('Install: py -m pip install pyserial',file=sys.stderr)
        return 2
    try:
        target=choose_port(list_ports,a.port)
        with serial.Serial(target,115200,timeout=1,write_timeout=2) as dev:
            dev.dtr=True
            time.sleep(.3)
            print('USB CDC:',target)
            print(command(dev,'HELLO'))
            print('Profile:',a.profile.upper())
            print('Disconnect ALL external devices on candidate GPIO pads.')
            print('Each D pin alone goes HIGH ~3.3V, then LOW ~0V. All others are inputs.')
            print('The firmware releases driven pins automatically after 120 seconds.')
            input('Press Enter to start (Ctrl+C cancels): ')
            for idx,gpio in enumerate(MAPS[a.profile]):
                print('\nD%d -> %s'%(idx,gpio))
                level=command(dev,'SET %s %d H'%(a.profile[0].upper(),idx))
                print('HIGH:',level)
                if level.startswith('SKIP '):
                    print('NFC pin reserved; not driving it')
                    input('Press Enter to skip: ')
                    continue
                if not level.startswith('OK '): raise RuntimeError(level)
                input('Measure physical pad voltage now (~3.3V); Enter -> LOW: ')
                level=command(dev,'SET %s %d L'%(a.profile[0].upper(),idx))
                print('LOW: ',level)
                if not level.startswith('OK '): raise RuntimeError(level)
                input('Measure physical pad voltage now (~0V); Enter -> next: ')
                print(command(dev,'OFF'))
            print('\nCompleted D0..D10. Compare measured labels to requested GPIO map.')
            return 0
    except KeyboardInterrupt:
        print('\nCancelled')
        return 130
    except Exception as e:
        print('ERROR:',type(e).__name__,e,file=sys.stderr)
        return 1
    finally:
        if 'dev' in locals() and dev.is_open:
            try: command(dev,'OFF')
            except Exception: pass

if __name__ == '__main__':
    raise SystemExit(main())
