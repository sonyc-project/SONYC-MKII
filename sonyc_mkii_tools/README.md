# sonyc_mkii_tools
Tools for use with the MkII

## Install

```bash
git clone https://github.com/sonyc-project/sonyc_mkii_tools.git
cd sonyc_mkii_tools

# install python reader
pip install -e .

# make and install mastermel to run in the background
cd master_mel
./install.sh
```

## Usage

```python
import mkiiread

with mkiiread.read() as reader:
    x = reader.get()  # returns immediately, None if nothing in queue
    if x is not None:
        print(x)

    # wait for the next element then get it
    x = reader.wait().get()
    print(x)

    # dump will get the entire queue at this moment and start a fresh queue
    xs = reader.dump()
    print(len(xs))
    for x in xs:
        print(x)

    # this is a
    for x in reader:
        print(x)
```

## Alternatives

```bash
cd master_mel
make
cp master_mel /usr/local/bin/
```

## Config Update
These are test vectors that Dhrubo sent to Nathan. Will fill in full config.

```python
## Local tests

{"agg_period": 20}
{"ml_op_time": 7}
{"ml_op_time": 3}
{"status_period": 50}
{"status_period": 1800}
{"calib_config": [1,2.5,3,4.78,0.519]}
{"detect_thresholds": [0.325, 0.301, 0.126, 0.011, 0.268, 0.292, 0.544, 0.172]}
{"mode": 0, "ml_op_time": 3, "status_period": 3600}
{"mode": 0, "ml_op_time": 3, "status_period": 3600, "spl_thresh": 10.0, "calib_config": [1,2.5,3,4.78,0.519]}

{
	"mode": 0, "ml_op_time": 3,
	"status_period": 3600,
	"spl_thresh": 10.0,
	"calib_config": [1,2.5,3,4.78,0.519],
	"detect_thresholds": [0.325, 0.301, 0.126, 0.011, 0.268, 0.292, 0.544, 0.172]
}


## Wireless tests - NO UNNECESSARY SPACES/TABS/NEWLINES IN WIRELESS CONTROL MESSAGES PLEASE. EVERY BYTE COUNTS. 
## Max. Payload Size: 109 bytes.

# Broadcast

{"mode": 1,"ml_op_time":3}
{"mode":1,"status_period":50}
{"mode":1,"agg_period":50}
{"mode":1,"calib_config":[0.1,2.5,3,4.78,0.519,3.78,1.34,9]}
{"mode":1,"detect_thresholds":[0.325,0.301,0.126,0.011,0.268,0.292,0.544,0.172]}
{"ml_op_time":3,"status_period":3600,"mode":1}
{"mode":1,"ml_op_time":3,"status_period":3600,"spl_thresh":10.0}

# Multicast

{"mode":1,"ml_op_time":3,"nodes":[27719,27718]}
{"mode":1,"status_period":3600,"nodes":[27718]}
{"mode":1,"ml_op_time":3,"status_period":3600,"spl_thresh":10.0,"nodes":[27719,27718]}
```
