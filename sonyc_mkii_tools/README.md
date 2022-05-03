# Install

```bash
git clone https://github.com/sonyc-project/SONYC-MKII.git
cd sonyc_mkii_tools

# install python reader
pip install -e .

# make and install mastermel to run in the background
cd master_mel
./install.sh
```

## Alternatives

```bash
cd master_mel
make
cp master_mel /usr/local/bin/
```

# Usage
## Reading data from the base station
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

## Pushing Configuration Updates Locally or Wirelessly
master_mel can also be used to push configuration updates to the MKII nodes in the sensor network from the base station. Configuration messages are JSON strings that can be piped through stdin or read from a JSON file. Examples:

` pi@raspberrypi:~/sonyc_mkii_tools/master_mel $ ./master_mel --dev=/dev/ttyS4 --listen --print-data-stdout --print-timestamps --send-data <<< '{"ml_op_time": 5}'`

`pi@raspberrypi:~/sonyc_mkii_tools/master_mel $ ./master_mel --dev=/dev/ttyS4 --listen --print-data-stdout --print-timestamps --send-data < test.json`


Configuration messages could be local (for the USB-connected base station only) or wireless (propagated wirelssly to all, or a subset of, MKII nodes from the base). Examples of supported config messages are given below. Note that wireless messages have a hard limit of 109 bytes due to the inherent bandwidth limitations of the LoRA network, therefore longer JSON commands should be broken down into smaller subsets.

```
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

## Loading new firmware
**The MKII can be loaded with new firmware with either USB or JTAG**, the latter being out of scope for this document.
From this, any software component can be updated. The bootloader can even erase/update/overwrite itself. The bootloader is generic and can be used to load arbitrary code subject to the limitations of the STM32H743 memory space.

To load new firmware via USB, the MKII must manually be placed into bootloader mode. The steps to enter bootloader mode are:
1. Press the reset button near the antenna connector (marked SW2 in silkscreen).
2. Wait for the first red LED blink (near the USB connector). Should be only a few seconds.
3. Press the other button (SW1). There is a window of several seconds to do this. If successful, the three red LEDs will light in sequence.

If the LED never lights up in step 2, then either the device has depleted batteries or has a corrupted bootloader and will require JTAG to restore functionality.

The next steps require `master_mel`. This tool is available in this github repo. It has been tested to Cygwin+Windows and the Raspberry Pi Model 3B. It was written for POSIX and is expected to work on any Unix-like platform that supports USB-CDC serial port mode. Simply `make` from its directory.

Once in bootloader mode, test it by sending a "hello". It should look like this. Note the command:
```
pi@raspberrypi:~/sonyc_mkii_tools/master_mel $ ./master_mel --dev /dev/ttyACM0 --send-hello
Opened port /dev/ttyACM0

H7 Bootloader Compiled Dec  1 2020 22:00:23
CPU UID: 0x004C00443039510338373539
Device Network ID 11823 (0x2E2F)

Done!
Got 4 frames
119 debug bytes
0 data bytes
0 audio bytes
```

On the Raspberry Pi model 3B, the USB device is almost always /dev/ttyACM0. Occasionally ttyACM1 is observed. If there are other CDC devices or you are on a different platform, you will have to determine what the proper device is on your own as it will vary with platform and Linux flavor. `dmesg` is a good place to start and will likely tell you what you need to know after a plug-in.

Next you must erase the desired memory region before writing new data. The STM32H743/753 is erased at the page level. There are 16 pages of 128 kByte each. In the MKII setup, the bootloader resides on Page 0. The OS occupies pages 1-14 inclusive. Finally the C# application bytecode is page 15. The format is to supply `master_mel` with a start and end page. When erasing page 0, the bootloader itself, the `--allow-unsafe` flag is required because a mistake in this case can corrupt the bootloader. As long as the bootloader is intact, it is possible to re-enter bootloader mode and try again if anything goes wrong. In the event that something does go wrong, for example a bad binary was written, simply start over from the erase step but *do not reboot the MKII until at least a valid bootloader is programmed*. 

Here is an example of doing a full chip erase, including the bootloader. (We will be replacing it in this example).
```
pi@raspberrypi:~/sonyc_mkii_tools/master_mel $ ./master_mel --dev /dev/ttyACM0 --erase-sector-start 0 --erase-sector-end 15 --allow-unsafe
Opened port /dev/ttyACM0
Erase operation completed in 9775 ms

Done!
Got 2 frames
39 debug bytes
0 data bytes
0 audio bytes
```

At this point the H7 flash (2 MByte) is completely erased. In the next example, we will load a pre-baked firmware in one shot:
```
pi@raspberrypi:~/sonyc_mkii_tools/master_mel $ ./master_mel --dev /dev/ttyACM0 --program-binary ~/sonyc_base_full.bin --program-addr 0x08000000
Opened port /dev/ttyACM0

Program operation completed in 20813 ms

Done!
Got 8194 frames
42 debug bytes
0 data bytes
0 audio bytes
```

**NOTE: Which method you use, the below or above, depends on what kind of .bin you have. If your .bin includes a new bootloader, use the above, otherwise use below. If unsure, verify as you must, because erasing the bootloader without immediate replacement (before rebooting) cannot be recovered easily (requires JTAG)**

The first command erases all Flash space except for the bootloader (sector 0) and so doesn't require the `--allow-unsafe` flag.
```
$ ./master_mel --dev /dev/ttyACM0 --erase-sector-start 1 --erase-sector-end 15
...
$ ./master_mel --dev /dev/ttyACM0 --program-binary ./sonyc_mkii.bin --program-addr 0x08020000
Opened port /dev/ttyACM0
Program operation completed in 9150 ms

Done!
Got 7158 frames
41 debug bytes
0 data bytes
0 audio bytes
```

A quick summary of the flags:

`--program-binary`: expects a binary format (.bin or .dat) file and is directly loaded to flash at the given address. Will attempt to write the whole file, so in no case should it exceed 2 MByte (flash size) or the size of the region you are attempting to write. By convention, C# bytecode has the .dat file extension and everything else is .bin

`--program-addr`: This is the memory (byte) address to being loading the above binary. The H7 flash begins at 0x08000000 and in this example `sonyc_base_full.bin` is a 2 MB binary dump of the full firmware including the bootloader, OS, and C# app.

At this point the MKII is fully programed and ready to go. Don't forget to reset it out of bootloader mode before use! (Alternatively, use `--boot` command).

**Other Examples**

Changing the C# firmware (e.g., change from Fence to Base firmware type)
```
pi@raspberrypi:~/sonyc_mkii_tools/master_mel $ ./master_mel --dev /dev/ttyACM0 --erase-sector-start 15 --erase-sector-end 15
pi@raspberrypi:~/sonyc_mkii_tools/master_mel $ ./master_mel --dev /dev/ttyACM0 --program-binary ~/new_c_sharp.dat --program-addr 0x081E0000
```
0x081E0000 is special for the MKII setup. It is the beginning of the 16th (last) page and is the address where the OS will look for (and execute) C# bytecode.
