Host side application for communicating with a SONYC MkII "Mel" device

### Config Schema
... include from drubo's doc
```

```

### `mastermel` app
This parses serial output and forwards over a UDP socket.
 - To build: `make`
 - To run: `./master_mel --dev /dev/ttyACM0 --listen --udp &> mastermel.out &`
 
```python
# TODO: haven't actually tested this in isolation
import json
import socket

def mkii_read(host='127.0.0.1', port=61393, timeout=10):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind((host, port))
        s.settimeout(timeout)
        with s.makefile() as sf:
            while True:
                data = decodeline(sf.readline())
                if data:
                    yield data


def decodeline(line):
    try:
        return line and json.loads(line)
    except json.JSONDecodeError as e:
        logger.warning(
            'Error parsing json line "%s": (%s) %s',
            line, type(e).__name__, str(e))
 ```


### Local/Remote Config Update Procedure
1. ...
