import os
import json
import time
import socket
import select
import logging
import datetime


log = logging.getLogger(__name__)
log.setLevel(logging.INFO)

HOST='127.0.0.1'
PORT=61393


class Reader:
    def __init__(self, host=HOST, port=PORT, chunksize=2**16-1, end=b'\n', throttle=0.1):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((host, port))
        self.buffer = bytearray()   
        self.chunksize = chunksize
        self.throttle = throttle
        self.end = end

    def __enter__(self): 
        return self

    def __exit__(self, *a):
        self.sock.__exit__(*a)

    def __call__(self, *a, **kw):
        return self.poll(*a, **kw)

    def poll(self, timeout=1):
        '''Pull messages until you get the next message'''
        t0 = time.time()
        while True:
            r, _, _ = select.select([self.sock], [], [self.sock], 0)
            if r:
                chunk = r[0].recv(self.chunksize)
                self.buffer.extend(chunk)
                if not chunk or self.end in chunk:
                    break
            if not timeout or time.time() - t0 > timeout:
                break
            if not r:
                time.sleep(self.throttle)
        return self

    def polln(self, *a, n=3, **kw):
        for _ in range(n):
            self.poll(*a, **kw)

    def __iter__(self):
        while self.end in self.buffer:
            i = self.buffer.find(self.end)
            line, self.buffer = self.buffer[:i], self.buffer[i + 1:]
            data = decodeline(line)
            if data:
                yield data


def run(*a, interval=5, convert=False, save=False, chunk=3, **kw):
    save = '.' if save is True else save
    with Reader(*a, **kw) as r:
        while True:
            t0 = time.time()
            statuses = r.polln(interval, n=chunk)
            statuses = [process(s) for s in statuses] if convert else statuses
            write_statuses(statuses, out_dir=save) if save else print_statuses(statuses)
            time.sleep(max(0, interval - (time.time() - t0)))


def write_statuses(statuses, fname='status-{sender_node_id}-T{time}.json', interval=3600, out_dir='.'):
    t = int(time.time() // interval)
    for status in statuses:
        fname = os.path.join(out_dir, fname.format(**status, time=t))
        with open(fname, 'a') as f:
            f.write(json.dumps(status) + '\n')
        return fname


def print_statuses(statuses):
    print('--- {} | found {} statuses | devices: {} ---'.format(
        datetime.datetime.now(), len(statuses), 
        ', '.join(map(str, sorted({s.get('sender_node_id') for s in statuses}))) or 'none'))
    if statuses:
        for s in statuses:
            print(s.get('sender_node_id'), s)
        print()


# message parsing

def decodeline(line):
    try:
        return line and json.loads(line)
    except json.JSONDecodeError as e:
        log.warning(
            'Error parsing json line "%s": (%s) %s',
            line, type(e).__name__, str(e))


FLATTEN_LISTS = {
    'sender_power': ('power', ['1h', '24h']),
    'sender_solar': ('solar', ['1h', '24h']),
    'sender_battery': ('battery', ['total', 'cell0', 'cell1', 'cell2', 'cell3']),
}

RENAME_FIELDS = {
    # base
    'spl': 'laeq',
    'class_probs': 'classification',
    # fence
    #'sender_temp': 'temp',
}

FLATTEN_DICTS = ['spl_stats']


def process(d, id=None, id_key='fqdn', sep='-', prefix='m2'):
    # set an ID key
    if id:
        d[id_key] = '{}{}{}-{}'.format(id, sep, prefix, d['sender_node_id'])
    # flatten list fields
    for k, (prefix, labels) in FLATTEN_LISTS.items(): 
        if k in d:
            for name, x in zip(labels, d.pop(k)):
                d['{}_{}'.format(prefix, name)] = x
    # rename fields
    for old, new in RENAME_FIELDS.items():
        if old in d:
            d[new] = d.pop(old)
    # flatten dict fields
    for k in FLATTEN_DICTS:
        if k in d:
            d.update(d.pop(k))
    # special cases o.o
    if 'payload' in d and 'dog' in d['payload']:
        d['classification'] = d.pop('payload')
    return d





def server(host=HOST, port=PORT, interval=5):
    msgs = [
        {'sender_node_id': 12345},
        {'sender_node_id': 12345},
    ]
    weights = [0.1, 0.9]
    import random
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind((host, port))
    while True:
        msg, = random.choices(msgs, k=1, weights=weights)
        print(msg)
        s.sendall(json.dumps(msg).encode())
        time.sleep(interval)



if __name__ == '__main__':
    import fire
    fire.Fire()