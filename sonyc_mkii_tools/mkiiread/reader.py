import json
import time
import logging
import functools
from . import util

log = logging.getLogger(__name__)
log.setLevel(logging.INFO)


@util.threaded_yield
def read(host='127.0.0.1', port=61393):
    import socket
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind((host, port))
        for line in util.readline(s):
            data = decodeline(line)
            if data:
                yield data
            else:
                log.info('could not decode message as json: {}'.format(line))
            time.sleep(0.05)


START = b'\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x00~~\x00\x01\x00\x00\x00'

@util.threaded_yield
def read_serial(device='/dev/ttyACM*', baudrate=19200, timeout=30, start=START, glob_interval=3):
    import serial
    fname = util.wait_for_glob(device, glob_interval)[0]
    with serial.Serial(fname, baudrate=baudrate, timeout=timeout) as dev:
        while True:
            l = dev.readline()
            if l.startswith(start + b'{'):
                l = l[len(start):].strip().replace(b'}]', b'}') # weird extra characters
                data = decodeline(l)
                if data:
                    yield data

def decodeline(line):
    try:
        return line and json.loads(line)
    except json.JSONDecodeError as e:
        log.warning(
            'Error parsing json line "%s": (%s) %s',
            line, type(e).__name__, str(e))


def merge(reader, base=None, **kw):
    return [_merge(v, base, **kw) for v in util.groupby(reader, 'sender_node_id').values()]


listfields = {
    'sender_power': ('power', ['1h', '24h']),
    'sender_solar': ('solar', ['1h', '24h']),
    'sender_battery': ('battery', ['total', 'cell0', 'cell1', 'cell2', 'cell3']),
}

renamefields = {
    # base
    'spl': 'laeq',
    'class_probs': 'classification',
    # fence
    # status
    #'sender_temp': 'temp',
}

mergedictfields = ['spl_stats']

def _merge(ds, out=None, id_key='fqdn', sep='-', prefix='m2'):
    out = dict(out or {})
    for i, d in enumerate(ds):
        if i == 0:
            out[id_key] = '{}{}{}-{}'.format(out[id_key], sep, prefix, d['sender_node_id'])

        # flatten list fields
        for k, (prefix, labels) in listfields.items(): 
            if k in d:
                for name, x in zip(labels, d.pop(k)):
                    d['{}_{}'.format(prefix, name)] = x

        # rename fields
        for old, new in renamefields.items():
            if old in d:
                d[new] = d.pop(old)

        # flatten dict fields
        for k in mergedictfields:
            if k in d:
                d.update(d.pop(k))

        # special cases o.o
        if 'payload' in d and 'dog' in d['payload']:
            d['classification'] = d.pop('payload')
        
        out.update(**d)
    return out


@functools.wraps(read)
def cli(*a, period=5, **kw):
    import datetime
    import glob
    print('devices:', glob.glob('/dev/ttyACM*'))
    log.setLevel(logging.DEBUG)
    with read(*a, **kw) as r:
        try:
            while r.is_open:
                statuses = r.dump()
                print('--- {} | found {} statuses | devices: {} ---'.format(
                    datetime.datetime.now(), len(statuses), 
                    ', '.join(map(str, sorted({s.get('sender_node_id') for s in statuses}))) or 'none'
                ))
                if statuses:
                    for s in statuses:
                        print(s.get('sender_node_id'), s)
                    print()
                time.sleep(period)
        except KeyboardInterrupt:
            print('\n--- done! ---')
