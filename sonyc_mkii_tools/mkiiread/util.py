import time
import glob
import functools
import threading
import collections


def threaded_yield(func):
    @functools.wraps(func)
    def inner(*a, **kw):
        return ThreadedGenerator(func(*a, **kw))
    return inner


class ThreadedGenerator:
    new_q = collections.deque
    def __init__(self, gen):
        self.gen = gen
        self.q = self.new_q()

    # thread management

    thread = None
    is_open = False
    def open(self):
        if not self.is_open:
            self.is_open = True
            self.thread = threading.Thread(target=self._run)
            self.thread.start()
        return self

    def close(self):
        if self.is_open:
            self.is_open = False
            try:
                self.gen.close()
            except ValueError:
                interrupt_thread(self.thread)
            self.thread.join()
        return self

    def _run(self):
        try:
            for x in self.gen:
                self.q.append(x)
                if not self.is_open:
                    break
        except KeyboardInterrupt:
            pass #print('interrupting thread')

    def __enter__(self):
        return self.open()

    def __exit__(self, *a):
        self.close()

    # queue access

    def get(self, block=False):
        if block:
            self.wait()
        return self.q.popleft() if self.q else None

    def __call__(self):
        '''Get all items from the queue.'''
        return self.open().dump()

    def __iter__(self):
        return self.open()

    def __next__(self):
        self.wait()
        if not self.is_open:
            raise StopIteration()
        return self.q.popleft()

    def dump(self):
        '''Get all items from the queue.'''
        q, self.q = self.q, self.new_q()
        return q

    def wait(self, timeout=None, n=1, throttle=0.1):
        '''Sleep until an item is in the queue.'''
        t0 = time.time()
        while not timeout or (time.time() - t0) < timeout:
            if not self.is_open or len(self.q) >= n:
                return self
            time.sleep(throttle)
        raise TimeoutError('Timed out with no items in queue.')


class Flag:
    def __init__(self, initial=False):
        self.initial = self.value = initial

    def __bool__(self):
        return bool(self.value)

    def set(self):
        self.value = not self.initial

    def unset(self):
        self.value = self.initial


def wait_for_glob(path, delay=0.1):
    fs = glob.glob(path)
    while not fs:
        time.sleep(delay)
        fs = glob.glob(path)
    return fs


def groupby(xs, key):
    if not callable(key):
        key, key_ = (lambda x: x.get(key_)), key
    out = {}
    for x in xs:
        k = key(x)
        if k not in out:
            out[k] = []
        out[k].append(x)
    return out


import select
def readline(sock, chunksize=2**16-1, end=b'\n', throttle=0.2, sentinel=None):
    buffer = bytearray()
    while True:
        while True:
            #print('calling select')
            r, _, _ = select.select([sock], [], [sock], 0)
            if r:
                chunk = r[0].recv(chunksize)
                buffer.extend(chunk)
                #print('got chunk:', chunk, repr(end), end in chunk, flush=True)
                if not chunk or end in chunk:
                    break
            else:
                #print('sleeping - nothing available')
                time.sleep(throttle)
        if buffer == sentinel:
            break
        while end in buffer:
            i = buffer.find(end)
            line, buffer = buffer[:i], buffer[i + 1:]
            yield line


import ctypes
import threading

def interrupt_thread(tobj, exception=KeyboardInterrupt):
    ret = ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_long(tobj.ident), ctypes.py_object(exception))
    if ret == 0:
        raise ValueError("Invalid thread ID")
    if ret > 1: # Huh? we punch a hole into C level interpreter, so clean up the mess.
        ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_long(tobj.ident), 0)
        raise SystemError("PyThreadState_SetAsyncExc failed")
